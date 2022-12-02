#!/usr/bin/python

import time, math, socket, os
import argparse, logging, yaml
from _thread import *
from queue import Queue

DEFAULT_LOG_LEVEL='info'
logger = logging.getLogger(__name__)

class testFunction(object):
    def __init__(self, fn_id, n_threads, fn_params, route, sockmap_server_ip, sockmap_server_port, rpc_server_ip, rpc_server_port):
        self.fn_id = fn_id
        self.n_threads = n_threads
        self.fn_params = fn_params # {'memory_mb': val, 'sleep_ms': val, 'compute': val}
        self.route = route
        logger.info("Initialize test function {} with {} worker threads".format(fn_id, n_threads))

        self.sockmap_server_ip   = sockmap_server_ip
        self.sockmap_server_port = sockmap_server_port
        self.rpc_server_ip     = rpc_server_ip
        self.rpc_server_port   = rpc_server_port

        logger.info('Connecting to sockmap server {}:{}...'.format(sockmap_server_ip, sockmap_server_port))
        self.sockmap_sock = self.sockmapClient(self.sockmap_server_ip, self.sockmap_server_port)

        self.pid = os.getpid()
        self.sock_fd = self.sockmap_sock.fileno()
        logger.info("SKMSG metadata: PID {}; socket FD {}; Fn ID {}".format(self.pid, self.sock_fd, self.fn_id))

        logger.info('Connecting to RPC server {}:{}...'.format(rpc_server_ip, rpc_server_port))
        self.rpc_sock = self.RpcClient(self.rpc_server_ip, self.rpc_server_port)

        skmsg_md = [self.pid, self.sock_fd, self.fn_id]
        skmsg_md_bytes = b''.join([skmsg_md[0].to_bytes(4, byteorder = 'little'), skmsg_md[1].to_bytes(4, byteorder = 'little'), skmsg_md[2].to_bytes(4, byteorder = 'little')])
        logger.debug("Socket metadata: {}".format(skmsg_md_bytes))

        self.rpc_sock.send(skmsg_md_bytes)
        self.rpc_sock.close()

        # TODO: attach to shared memory pool
        # print("Remap shared memory pool")
        # self.shm_pool = {}
        # self.init_shm_pool()

        logger.info("Initialize {} RX pipes and {} TX pipes".format(n_threads, n_threads))
        self.rx_queues = []
        self.tx_queues = []
        for i in range(0, n_threads):
            self.rx_queues.append(Queue())
            self.tx_queues.append(Queue())

    def run(self):
        logger.info("Start worker threads")
        for i in range(0, self.n_threads):
            start_new_thread(self.nf_worker, (i, self.rx_queues[i], self.tx_queues[i], ))

        # Start RX/TX threads
        start_new_thread(self.io_rx, (self.sockmap_sock, ))
        self.io_tx(self.sockmap_sock) # start_new_thread(self.io_tx, (sock, ))

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

        sock.connect((remote_ip, port))
        logger.info('Connected to sockmap server {}:{}'.format(remote_ip, port))

        return sock

    def autoscale_sleep(self, interval):
        logger.debug("Function {} sleeps for {} seconds".format(self.fn_id, interval))
        time.sleep(interval)

    def autoscale_compute(self, num):
        for i in range(2, math.sqrt(num)):
            if (n % i) == 0:
                break 

    def io_rx(self, sock):
        logger.info("Function {} starts RX thread".format(self.fn_id))
        n_rx_req = 0
        rx_q_id = 0
        while(1):
            # Receiving SKMSG from socket
            rx_q_id = (rx_q_id + 1) % self.n_threads
            skmsg_md_bytes = sock.recv(1024).strip()
            logger.debug("RX thread-{} received #{} request".format(rx_q_id, n_rx_req))

            # Parse SKMSG; Check if SKMSG is allowed or not
            target_fn_id = int.from_bytes(skmsg_md_bytes[0:3], "little")
            # TODO: parse shm_obj_name from skmsg_md_bytes
            shm_obj_name = int.from_bytes(skmsg_md_bytes[4:7], "little")
            if target_fn_id != self.fn_id:
                logger.info("WARNING: Fn#{} received unexpected SKMSG [{}:{}]".format(self.fn_id, target_fn_id, shm_obj_name))
            logger.debug("Fn#{} received SKMSG [{}:{}]".format(self.fn_id, target_fn_id, shm_obj_name))
            n_rx_req = n_rx_req + 1

            # Handover descriptor to the worker thread
            self.rx_queues[rx_q_id].put(shm_obj_name)

    def io_tx(self, sock):
        logger.info("Function {} starts TX thread".format(self.fn_id))
        n_tx_req = 0
        tx_q_id = 0
        while(1):
            # Receiving descriptor from worker thread
            tx_q_id = (tx_q_id + 1) % self.n_threads
            shm_obj_name = self.tx_queues[tx_q_id].get()
            logger.debug("TX thread-{} received #{} request".format(tx_q_id, n_tx_req))
            n_tx_req = n_tx_req + 1

            # Preparing SKMSG for next hop
            next_fn = 0 # Hard code the next step as SPRIGHT gateway
            # TODO: use shm_obj_name to replace "n_tx_req" in skmsg_md_bytes
            # Different shm_obj_name must have same size
            skmsg_md_bytes = b''.join([next_fn.to_bytes(4, byteorder = 'little'), \
                                       n_tx_req.to_bytes(4, byteorder = 'little')])
            
            # Send SKMSG to next hop
            logger.debug("SKMSG {}".format(skmsg_md_bytes))
            sock.sendall(skmsg_md_bytes)

    def nf_worker(self, worker_thx_id, rx_q, tx_q):
        logger.debug("Worker thread {} is running".format(worker_thx_id))
        n_worker_req = 0
        while(1):
            # Receiving descriptor from RX thread
            shm_obj_name = rx_q.get()
            logger.debug("Worker thread-{} received #{} request".format(worker_thx_id, n_worker_req))
            n_worker_req = n_worker_req + 1

            # TODO: Using shm_obj_name to access shared memory
            # print the shared memory obj
            # shm_obj = self.shm_pool[shm_obj_name]
            # TODO: Performing application logic
            self.autoscale_sleep(self.fn_params['sleep_ms'])

            # Send descriptor to TX thread
            tx_q.put(shm_obj_name)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='A test python function for SPRIGHT')
    parser.add_argument('--config-file', help='Path of the config file')
    parser.add_argument('--fn-id', help='Function ID', type=int)
    # parser.add_argument('--n-threads', help='# of worker threads', type=int)
    parser.add_argument('--log-level', help='Log level', default = DEFAULT_LOG_LEVEL)
    args = parser.parse_args()
    logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.getLevelName(args.log_level.upper()))

    with open(args.config_file) as config_file:
        config = yaml.load(config_file)
        logger.debug("Config %s", config)
        sockmap_mgr_config = config['sockmap_manager']
        fn_config = config['function_metadata']
        route_config = config['routes']
        route_id = config['route_id']
        route = route_config[route_id - 1]['sequence']
        logger.info("Using {}: {}".format(route_config[route_id - 1]['route_name'], route_config[route_id - 1]['sequence']))

        for i in range(0, len(fn_config)):
            if args.fn_id == fn_config[i]['fn_id']:
                n_threads = fn_config[i]['n_threads']
                fn_params = fn_config[i]['params'] # {'memory_mb': val, 'sleep_ms': val, 'compute': val}
                logger.info("Function#{}: {}, {} threads ".format(args.fn_id, fn_config[i]['fn_name'], n_threads))
                func = testFunction(args.fn_id, n_threads, fn_params, route, sockmap_mgr_config['sockmap_server_ip'], sockmap_mgr_config['sockmap_server_port'], sockmap_mgr_config['rpc_server_ip'], sockmap_mgr_config['rpc_server_port'])
                func.run()

        logger.warning("Function#{} has no matched configuration".format(args.fn_id))