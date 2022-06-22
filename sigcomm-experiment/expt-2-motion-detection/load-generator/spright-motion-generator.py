import argparse
import datetime
import os
import threading
import requests, time

# ----------------------------------------------- #
# --------------- Configuration ----------------- #
# ----------------------------------------------- #
parser = argparse.ArgumentParser()
parser.add_argument('-i', '--ip', action='store', type=str, default='127.0.0.1')
parser.add_argument('-p', '--port', action='store', type=str, default='8080')
args = parser.parse_args()

URL = "http://" + args.ip + ":" + args.port + "/1/"
base = 14 # file name
day = 1   # Days of data to be used
scale_rate = 1 # Scale the time scale of the data
max_run_time_sec = 3600

# Generate data path
prefix = os.getcwd() + '/merl/01'
path = prefix + str(base) + '.txt'
print(path)

# ----------------------------------------------- #
# -------------- Pre-process data --------------- #
# ----------------------------------------------- #
s, e = [], []
begin = None
with open(path, 'r') as f:
    for line in f:
        _, start_unix, stop_unix, _ = line.split()
        s.append(start_unix)
        # print(int(start_unix) - begin, int(stop_unix) - begin)
print("Dataset initialization done")
print("Total {} records".format(len(s)))
print("Average records per second: {}".format(float(len(s)/1728000))) # 20 days in total
print("Use {} day(s) data".format(day))

s.sort()

# 2-way merge
res = []
begin = int(s[0])
for u in s:
    detal_t = float(int(u) - begin)/1000
    if detal_t < 86400 * int(day):
        res.append(detal_t)

sleep_t = []
for i in range(0, len(res)):
    if i == 0:
        sleep_t.append(res[i])
    else:
        sleep_t.append(res[i]-res[i-1])
print("Total {} events within {} day(s). The average event generation rate is {} events/second".format(len(res), day, len(res)/(86400 * int(day))))
print("Event rate (after scaling): {}".format( len(res)/(86400 * int(day)) * scale_rate ))
print("Time to complete all the events: {} seconds".format((86400 * int(day)) / scale_rate))
print("\n* * * * * * * EVENT GENERATION BEGIN * * * * * *")

# ----------------------------------------------- #
# -------------- Open output file --------------- #
# ----------------------------------------------- #
stat_file = open('skmsg_motion_output.csv', 'w')

def post(http_url, output_file, send_time):
    print("Send a request at {}".format(send_time))
    r = requests.get(url = http_url)
    print(str(time.time()) + ";" + str(r.elapsed.total_seconds()))
    output_file.write(str(time.time()) + ";" + str(r.elapsed.total_seconds()) + "\n")

# ----------------------------------------------- #
# -------------- Read sleep time ---------------- #
# ----------------------------------------------- #
total_sec = 0
try:
    for st in sleep_t:
        # Send request to function chain
        th = threading.Thread(target=post, args=(URL, stat_file, total_sec))
        th.daemon = True
        th.start()
        # r = requests.get(url = URL)
        # print(str(time.time()) + ";" + str(r.elapsed.total_seconds()))
        # stat_file.write(str(time.time()) + ";" + str(r.elapsed.total_seconds()) + "\n")
        time.sleep(st)
        # print("Sleep for {}".format(st))
        total_sec = total_sec + st
        if total_sec > max_run_time_sec:
            break
except KeyboardInterrupt:
    stat_file.close()
    exit(1)
stat_file.close()
exit("All the events in {} day(s) have been sent out".format(day))
