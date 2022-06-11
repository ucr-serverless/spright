#!/usr/bin/python
#
# Copyright 2018 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import random, time
from locust import HttpUser, TaskSet, between, events

products = [
    '0PUK6V6EV0',
    '1YMWWN1N4O',
    '2ZYFJ3GM2N',
    '66VCHSJNUP',
    '6E92ZMYYFZ',
    '9SIQT8TOJO',
    'L9ECAV7KIM',
    'LS4PSXUNUM',
    'OLJCESPC7Z']

def index(l):
    l.client.get("/", headers={"host": "kn-frontend.default.example.com"})

def setCurrency(l):
    currencies = ['EUR', 'USD', 'JPY', 'CAD']
    l.client.post("/setCurrency",
        {'currency_code': random.choice(currencies)}, headers={"host": "kn-frontend.default.example.com"})

def browseProduct(l):
    l.client.get("/product/" + random.choice(products), headers={"host": "kn-frontend.default.example.com"})

def viewCart(l):
    l.client.get("/cart", headers={"host": "kn-frontend.default.example.com"})

def addToCart(l):
    product = random.choice(products)
    l.client.get("/product/" + product, headers={"host": "kn-frontend.default.example.com"})
    l.client.post("/cart", {
        'product_id': product,
        'quantity': random.choice([1,2,3,4,5,10])}, headers={"host": "kn-frontend.default.example.com"})

def checkout(l):
    addToCart(l)
    l.client.post("/cart/checkout", {
        'email': 'someone@example.com',
        'street_address': '1600 Amphitheatre Parkway',
        'zip_code': '94043',
        'city': 'Mountain View',
        'state': 'CA',
        'country': 'United States',
        'credit_card_number': '4432801561520454',
        'credit_card_expiration_month': '1',
        'credit_card_expiration_year': '2039',
        'credit_card_cvv': '672',
    }, headers={"host": "kn-frontend.default.example.com"})

class UserBehavior(TaskSet):

    def on_start(self):
        index(self)

    tasks = {index: 1,
        setCurrency: 2,
        browseProduct: 10,
        addToCart: 2,
        viewCart: 3,
        checkout: 1}

class WebsiteUser(HttpUser):
    tasks = [UserBehavior]
    wait_time = between(1, 10)

stat_file = open('stats.csv', 'w')

@events.request_success.add_listener
def hook_request_success(request_type, name, response_time, response_length, **kw):
    # print(request_type + ";" + name + ";" + str(response_time) + ";" + str(response_length) + "\n")
    #stat_file.write(request_type + ";" + name + ";" + str(response_time) + ";" + str(response_length) + "\n")
    #print(int(time.time()))
    stat_file.write(str(time.time()) + ";" + request_type + ";" + name + ";" + str(response_time) + "\n")

@events.quitting.add_listener
def hook_quitting(environment, **kw):
    stat_file.close()