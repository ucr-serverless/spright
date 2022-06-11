import argparse
import datetime
import os
import threading
import requests, time, random

# ----------------------------------------------- #
# --------------- Configuration ----------------- #
# ----------------------------------------------- #
parser = argparse.ArgumentParser()
parser.add_argument('-i', '--ip', action='store', type=str, default='127.0.0.1')
parser.add_argument('-p', '--port', action='store', type=str, default='80')
args = parser.parse_args()

URL = "http://" + args.ip + ":" + args.port + "/"

# ----------------------------------------------- #
# -------------- Open output file --------------- #
# ----------------------------------------------- #
global stat_file 
stat_file = open('skmsg.parking_output.csv', 'w')

def post(http_url, send_time):
    #print("Send a request at {}".format(send_time))
    files = {'file': open('image.jpeg', 'rb')}
    tmp_url = ''
    if random.random() < 0.9:
        tmp_url = URL + '2/'
    else:
        tmp_url = URL + '1/'
    r = requests.post(url = tmp_url, files=files)
    #body_len = len(r.request.body if r.request.body else [])
    #print(body_len)
    #print(str(time.time()) + ";" + str(r.elapsed.total_seconds()))
    lock.acquire()
    stat_file.write(str(time.time()) + ";" + str(r.elapsed.total_seconds()) + "\n")
    lock.release()

# ----------------------------------------------- #
# -------------- Read sleep time ---------------- #
# ----------------------------------------------- #
lock=threading.Lock()

max_run_time_sec = 600
total_sec = 0
st_1 = 220
st_2 = 20
try:
    while(1):
        print("Send snapshots at {}".format(total_sec))
        # Send request to function chain
        for i in range(0, 164):
            th = threading.Thread(target=post, args=(URL, total_sec))
            th.daemon = True
            th.start()
        time.sleep(st_1 + st_2)
        # print("Sleep for {}".format(st))
        total_sec = total_sec + st_1 + st_2
        if total_sec >= max_run_time_sec:
            break
except KeyboardInterrupt:
    stat_file.close()
    exit(1)
stat_file.close()
