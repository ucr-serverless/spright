#!/usr/bin/python

import time, socket, os
import argparse, logging, yaml
from _thread import *

# from bcc import BPF, BPFAttachType, lib

DEFAULT_LOG_LEVEL='info'
logger = logging.getLogger(__name__)

class testGateway(object):
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

    def run(self):
        print('Gateway is running..')

        start_new_thread(self.gw_rx, (self.sockmap_sock, ))
        self.gw_tx(self.sockmap_sock)

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

    def gw_rx(self, sock):
        print("Gateway starts RX thread")

        while(1):
            # shm_obj_name = sock.recv(1024).strip()
            skmsg_md_bytes = sock.recv(1024).strip()
            print("Gateway completes #{} request: {}".format(self.succ_req, skmsg_md_bytes))
            self.succ_req = self.succ_req + 1

    def gw_tx(self, sock):
        print("Gateway starts TX thread")
        
        # Hard code the next fn id as 1
        next_fn = 2

        while(1):
            skmsg_md_bytes = b''.join([next_fn.to_bytes(4, byteorder = 'little'), \
                                       self.succ_req.to_bytes(4, byteorder = 'little')])
            sock.sendall(skmsg_md_bytes)
            time.sleep(3)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='A test SPRIGHT Gateway')
    parser.add_argument('--config-file', help='Path of the config file')
    parser.add_argument('--log-level', help='Log level', default = DEFAULT_LOG_LEVEL)
    args = parser.parse_args()
    logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.getLevelName(args.log_level.upper()))

    with open(args.config_file) as config_file:
        config = yaml.load(config_file)
        logger.debug("Config %s", config)
        gw = testGateway(config['sockmap_server_ip'], config['sockmap_server_port'], config['rpc_server_ip'], config['rpc_server_port'])
    gw.run()

    # Print bpf trace logs
    while True:
        try:
            # bpf.trace_print()
            time.sleep(1)
        except KeyboardInterrupt:
            sys.exit(0)