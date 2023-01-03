#!/usr/bin/python

import time, socket, os, sys
import argparse, logging, yaml
from _thread import *
from shared_memory_dict import SharedMemoryDict
from multiprocessing import shared_memory
import grpc
import unary_pb2_grpc as pb2_grpc
import unary_pb2 as pb2
from concurrent import futures
import threading
from queue import Queue
from threading import Thread

from http.server import BaseHTTPRequestHandler, HTTPServer

DEFAULT_LOG_LEVEL='info'
logger = logging.getLogger(__name__)

# Expose gw as a global obj to be referred by http handler
global gw
global write_queue
global free_queue
global shm_obj_queue

class SPRIGHTGateway(object):
    def __init__(self, route, sockmap_server_ip, sockmap_server_port, rpc_server_ip, rpc_server_port, smm_server_ip, smm_server_port):
        self.sockmap_server_ip   = sockmap_server_ip
        self.sockmap_server_port = sockmap_server_port
        self.rpc_server_ip     = rpc_server_ip
        self.rpc_server_port   = rpc_server_port
        self.smm_server_ip     = smm_server_ip
        self.smm_server_port   = smm_server_port

        self.route = route

        self.succ_req = 0
        logger.info("Initialize SPRIGHT Gateway")

        logger.info('Connecting to sockmap server {}:{}...'.format(sockmap_server_ip, sockmap_server_port))
        self.sockmap_sock = self.sockmapClient(self.sockmap_server_ip, self.sockmap_server_port)

        self.pid = os.getpid()
        self.sock_fd = self.sockmap_sock.fileno()
        self.fn_id = 0
        logger.info("SKMSG metadata: PID {}; socket FD {}; Fn ID {}".format(self.pid, self.sock_fd, self.fn_id))

        logger.info('Connecting to RPC server {}:{}...'.format(rpc_server_ip, rpc_server_port))
        self.rpc_sock = self.RpcClient(self.rpc_server_ip, self.rpc_server_port)

        skmsg_md = [self.pid, self.sock_fd, self.fn_id]
        skmsg_md_bytes = b''.join([skmsg_md[0].to_bytes(4, byteorder = 'little'), skmsg_md[1].to_bytes(4, byteorder = 'little'), skmsg_md[2].to_bytes(4, byteorder = 'little')])
        logger.debug("Socket metadata: {}".format(skmsg_md_bytes))

        self.rpc_sock.send(skmsg_md_bytes)
        self.rpc_sock.close()

        # Attach to shared memory pool
        logger.info('Connecting to SMM server {}:{}...'.format(smm_server_ip, smm_server_port))
        self.smm_sock = self.SmmClient(self.smm_server_ip, self.smm_server_port)
        self.shm_free_dict_name = self.smm_sock.recv(1024).decode("utf-8")
        logger.debug("SMM shm_free_dict_name: {}".format(self.shm_free_dict_name))
        self.smm_sock.close()

        logger.debug("Attaching to shared mem dict...")
        self.shm_free_dict = SharedMemoryDict(name = self.shm_free_dict_name, size = 32000) # TODO: get size as well

        # Creating a pool of pre-attached shm blocks
        self.shm_obj_pool = {}
        for key in self.shm_free_dict.keys():
            shm_obj = shared_memory.SharedMemory(key)
            self.shm_obj_pool[key] = shm_obj

        logger.info('Gateway is running..')

    def createSocket(self):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        except socket.error as msg:
            print('Failed to create socket. Error code: {}, Error message: {}'.format(str(msg[0]), msg[1]))
            sys.exit()

        return sock

    def RpcClient(self, remote_ip, port):
        sock = self.createSocket()

        sock.connect((remote_ip, port))
        logger.info('Connected to RPC server {}:{}'.format(remote_ip, port))

        return sock

    def sockmapClient(self, remote_ip, port):
        sock = self.createSocket()
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

        sock.connect((remote_ip, port))
        logger.info('Connected to sockmap server {}:{}'.format(remote_ip, port))

        return sock

    def SmmClient(self, remote_ip, port):
        sock = self.createSocket()

        sock.connect((remote_ip, port))
        logger.info('Connected to SHM server {}:{}'.format(remote_ip, port))

        return sock

    def write_to_free_block(self, content_length, binary_data):
        free_item = self.shm_free_dict.popitem()
        shm_obj_name = free_item[0]
        logger.debug("free_shm_obj_name: {}".format(shm_obj_name))

        shm_obj = self.shm_obj_pool[shm_obj_name]
        shm_obj.buf[:content_length] = binary_data
        # shm_obj.close()
        return shm_obj_name

    def gw_rx(self):
        skmsg_md_bytes = self.sockmap_sock.recv(1024)
        logger.debug("Gateway completes #{} request: {}".format(self.succ_req, skmsg_md_bytes))
        self.succ_req = self.succ_req + 1

    def gw_tx(self, next_fn, cur_hop, shm_obj_name):
        logger.debug("Gateway TX thread sends SKMSG to Fn#{}".format(next_fn))
        next_hop = cur_hop + 1
        skmsg_md_bytes = b''.join([next_fn.to_bytes(4, byteorder = 'little'), \
                                   next_hop.to_bytes(4, byteorder = 'little'), \
                                   shm_obj_name.encode("utf-8")])
        self.sockmap_sock.sendall(skmsg_md_bytes)

    def router(self, cur_hop):
        if cur_hop == len(self.route):
            # NOTE: The route ends. Back to Gateway
            next_fn = 0
        else:
            next_fn = self.route[cur_hop]
        logger.debug("Routing result: current hop#{}, next function ID is {}".format(cur_hop, next_fn))
        return next_fn

    # core() is the frontend of SPRIGHT gateway
    # It's used to interact between the http handler and function chains
    def core(self, shm_obj_name):
        cur_hop = 0 # NOTE: The function chain always starts from hop#0
        next_fn = self.router(cur_hop)
        if next_fn == 0:
            logger.debug("No route found. Gateway returns response directly")
            return
        self.gw_tx(next_fn, cur_hop, shm_obj_name)

        self.gw_rx()

class httpHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        logger.debug("SPRIGHT Gateway is handling GET request")

        # Write request into a shared memory object
        shm_obj_name = gw.write_to_free_block(content_length = 3, binary_data = b'xyz')

        # Handover request to SPRIGHT gateway core
        gw.core(shm_obj_name)

        # Recycle the used shm_obj
        gw.shm_free_dict[shm_obj_name] = 'FREE'

        logger.debug("SPRIGHT Gateway prepares a response")
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(bytes("<html><head><title>https://pythonbasics.org</title></head>", "utf-8"))
        self.wfile.write(bytes("<p>Request: %s</p>" % self.path, "utf-8"))
        self.wfile.write(bytes("<body>", "utf-8"))
        self.wfile.write(bytes("<p>This is an example web server.</p>", "utf-8"))
        self.wfile.write(bytes("</body></html>", "utf-8"))

    def do_POST(self):
        logger.debug("SPRIGHT Gateway is handling POST request")
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(bytes("<html><head><title>https://pythonbasics.org</title></head>", "utf-8"))
        self.wfile.write(bytes("<p>Request: %s</p>" % self.path, "utf-8"))
        self.wfile.write(bytes("<body>", "utf-8"))
        self.wfile.write(bytes("<p>This is an example web server.</p>", "utf-8"))
        self.wfile.write(bytes("</body></html>", "utf-8"))

class UnaryService(pb2_grpc.UnaryServicer):

    def __init__(self, *args, **kwargs):
        pass

    def GetServerResponse(self, request, context):

        # time.sleep(0.1)

        logger.debug("SPRIGHT Gateway is handling GET request")

        thread_name =  threading.currentThread().getName()
        logger.debug("Current thread:{}".format(thread_name))

        # shm_obj_name = gw.write_to_free_block(content_length = 3, binary_data = b'xyz')
        # add payload to write q here instead of write_to_free_block call
        write_queue.put((3, b'xyz'))

        
        # get shm_obj_name from the shm thread's queue
        logger.debug("GET handler of {}::shm_obj_queue:{}".format(thread_name,list(shm_obj_queue.queue)))
        logger.debug("write_queue:{}".format(list(write_queue.queue)))

        shm_obj_name = shm_obj_queue.get()

        logger.debug("GET handler::shm_obj_name:{}".format(shm_obj_name))

        # Handover request to SPRIGHT gateway core
        gw.core(shm_obj_name) 

        # Recycle the used shm_obj
        free_queue.put(shm_obj_name)
        # gw.shm_free_dict[shm_obj_name] = 'FREE'

        logger.debug("SPRIGHT Gateway prepares a response")
        message = request.message
        resp = "Received msg: " + message
        result = {'message': resp, 'received': True}

        return pb2.MessageResponse(**result)


def shm_consumer(write_q, free_q, shm_obj_q):
    while True:
        try:
            # write to free block based on data from write_queue
            logger.debug("shm_consumer::write_queue:{}".format(list(write_q.queue)))
            data = write_q.get()
            shm_obj_name = gw.write_to_free_block(content_length = data[0], binary_data = data[1])
            shm_obj_q.put(shm_obj_name)
        except Queue.empty:
            logger.debug("queue empty exception during write_to_free_block from write_queue")
        except:
            logger.debug("exception during write_to_free_block from write_queue")

        try:
            # free up & recyclce used block from free_queue
            logger.debug("shm_consumer::free_queue:{}".format(list(free_q.queue)))
            used_shm_obj_name = free_q.get()
            gw.shm_free_dict[used_shm_obj_name] = 'FREE'
        except Queue.empty:
            logger.debug("queue empty exception during freeing/recycling used used_shm_obj from free_queue")
        except:
            logger.debug("exception during freeing/recycling used used_shm_obj from free_queue")



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description = 'A test SPRIGHT Gateway')
    parser.add_argument('--config-file', help = 'Path of the config file')
    parser.add_argument('--log-level', help = 'Log level', default = DEFAULT_LOG_LEVEL)
    args = parser.parse_args()
    logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.getLevelName(args.log_level.upper()))

    with open(args.config_file) as config_file:
        # Loading configurations of SPRIGHT gateway and HTTP frontend
        config = yaml.load(config_file)
        logger.debug("Config %s", config)

        spright_cp_config = config['spright_control_plane']
        route_config = config['routes']
        route_id = config['route_id']
        route = route_config[route_id - 1]['sequence']
        logger.info("Using {}: {}".format(route_config[route_id - 1]['route_name'], route_config[route_id - 1]['sequence']))

        # Creating a SPRIGHT gateway object
        gw = SPRIGHTGateway(route, spright_cp_config['sockmap_server_ip'], \
                            spright_cp_config['sockmap_server_port'], \
                            spright_cp_config['rpc_server_ip'], \
                            spright_cp_config['rpc_server_port'], \
                            spright_cp_config['smm_server_ip'], \
                            spright_cp_config['smm_server_port'])

        write_queue = Queue()
        free_queue = Queue()
        shm_obj_queue = Queue()

        shm_thread = Thread(target = shm_consumer, args =(write_queue, free_queue, shm_obj_queue))
        shm_thread.daemon = True
        shm_thread.start()

        logger.info("shm_thread is running...")

        # # Starting the HTTP frontend
        # server = HTTPServer(('', 8080), httpHandler)
        # server.serve_forever()
        # logger.info("HTTP server is running...")
        
        # Starting the gRPC frontend
        logger.info("gRPC server is starting...")
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
        pb2_grpc.add_UnaryServicer_to_server(UnaryService(), server)
        server.add_insecure_port('[::]:50051')
        server.start()
        server.wait_for_termination()




    # Print bpf trace logs
    while True:
        try:
            # bpf.trace_print()
            time.sleep(1)
        except KeyboardInterrupt:
            server.stop()
            print("Gateway stopped.")
            sys.exit(0)