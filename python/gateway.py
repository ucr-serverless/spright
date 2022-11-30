#!/usr/bin/python

import time, socket, os
import argparse, logging, yaml
from _thread import *

# from bcc import BPF, BPFAttachType, lib
from http.server import BaseHTTPRequestHandler, HTTPServer

DEFAULT_LOG_LEVEL='info'
logger = logging.getLogger(__name__)

# Expose gw as a global obj to be referred by http handler
global gw

class SPRIGHTGateway(object):
    def __init__(self, sockmap_server_ip, sockmap_server_port, rpc_server_ip, rpc_server_port):
        self.sockmap_server_ip   = sockmap_server_ip
        self.sockmap_server_port = sockmap_server_port
        self.rpc_server_ip     = rpc_server_ip
        self.rpc_server_port   = rpc_server_port

        self.succ_req = 0
        print("Initialize test Gateway")

        print('Connecting to sockmap server {}:{}...'.format(sockmap_server_ip, sockmap_server_port))
        self.sockmap_sock = self.sockmapClient(self.sockmap_server_ip, self.sockmap_server_port)

        self.pid = os.getpid()
        self.sock_fd = self.sockmap_sock.fileno()
        self.fn_id = 0
        print("SKMSG metadata: PID {}; socket FD {}; Fn ID {}".format(self.pid, self.sock_fd, self.fn_id))

        print('Connecting to RPC server {}:{}...'.format(rpc_server_ip, rpc_server_port))
        self.rpc_sock = self.RpcClient(self.rpc_server_ip, self.rpc_server_port)

        skmsg_md = [self.pid, self.sock_fd, self.fn_id]
        skmsg_md_bytes = b''.join([skmsg_md[0].to_bytes(4, byteorder = 'little'), skmsg_md[1].to_bytes(4, byteorder = 'little'), skmsg_md[2].to_bytes(4, byteorder = 'little')])
        print(skmsg_md_bytes)

        self.rpc_sock.send(skmsg_md_bytes)
        self.rpc_sock.close()

        # TODO: attach to shared memory pool
        # print("Remap shared memory pool")
        # self.shm_pool = {}
        # self.init_shm_pool()

        print('Gateway is running..')

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
        print('Connected to RPC server {}:{}'.format(remote_ip, port))

        return sock

    def sockmapClient(self, remote_ip, port):
        sock = self.createSocket()
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

        sock.connect((remote_ip, port))
        print('Connected to sockmap server {}:{}'.format(remote_ip, port))

        return sock

    def gw_rx(self):
        skmsg_md_bytes = self.sockmap_sock.recv(1024).strip()
        print("Gateway completes #{} request: {}".format(self.succ_req, skmsg_md_bytes))
        self.succ_req = self.succ_req + 1

    def gw_tx(self, next_fn):
        print("Gateway TX thread sends SKMSG to {}".format(next_fn))
        skmsg_md_bytes = b''.join([next_fn.to_bytes(4, byteorder = 'little'), \
                                    self.succ_req.to_bytes(4, byteorder = 'little')])
        self.sockmap_sock.sendall(skmsg_md_bytes)
    
    # core() is the frontend of SPRIGHT gateway
    # It's used to interact between the http handler and function chains
    def core(self):
        # TODO: add routing logic
        # Hard code the next fn id as 1
        next_fn = 1
        self.gw_tx(next_fn)

        while(1):
            self.gw_rx()

            # TODO: add routing logic
            next_fn = 0 # Testing only
            if next_fn == 0:
                break
            else:
                self.gw_tx(next_fn)
    
class httpHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        print("SPRIGHT Gateway received a new request")

        # Handover request to SPRIGHT gateway core
        gw.core()

        print("SPRIGHT Gateway prepares a response")
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(bytes("<html><head><title>https://pythonbasics.org</title></head>", "utf-8"))
        self.wfile.write(bytes("<p>Request: %s</p>" % self.path, "utf-8"))
        self.wfile.write(bytes("<body>", "utf-8"))
        self.wfile.write(bytes("<p>This is an example web server.</p>", "utf-8"))
        self.wfile.write(bytes("</body></html>", "utf-8"))

    def do_POST(self):
        print("SPRIGHT Gateway prepares a response")
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(bytes("<html><head><title>https://pythonbasics.org</title></head>", "utf-8"))
        self.wfile.write(bytes("<p>Request: %s</p>" % self.path, "utf-8"))
        self.wfile.write(bytes("<body>", "utf-8"))
        self.wfile.write(bytes("<p>This is an example web server.</p>", "utf-8"))
        self.wfile.write(bytes("</body></html>", "utf-8"))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='A test SPRIGHT Gateway')
    parser.add_argument('--config-file', help='Path of the config file')
    parser.add_argument('--log-level', help='Log level', default = DEFAULT_LOG_LEVEL)
    args = parser.parse_args()
    logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.getLevelName(args.log_level.upper()))

    with open(args.config_file) as config_file:
        # Loading configurations of SPRIGHT gateway and HTTP frontend
        config = yaml.load(config_file)
        logger.debug("Config %s", config)

        # Creating a SPRIGHT gateway object
        gw = SPRIGHTGateway(config['sockmap_server_ip'], config['sockmap_server_port'], config['rpc_server_ip'], config['rpc_server_port'])

        # Starting the HTTP frontend
        server = HTTPServer(('', 8080), httpHandler)
        server.serve_forever()
        print("HTTP server is running...")

    # Print bpf trace logs
    while True:
        try:
            # bpf.trace_print()
            time.sleep(1)
        except KeyboardInterrupt:
            server.server_close()
            print("Gateway stopped.")
            sys.exit(0)