#!/usr/bin/env python3
# CO2-Monitor Daemon
#
# Polls CO2 values by invoking 'monitor -l' periodically and publishes them to
# a specific MQTT topic.
#
# TODO
# - Use config file to store constants

import json
import subprocess
import time
import paho.mqtt.client as mqtt

MQTT_HOST = 'power-pi.fd'
MQTT_TOPIC_CO2 = 'sensors/lounge/co2'
POLL_INTERVAL = 5  # seconds
MONITOR_BIN = '/home/co2-monitor/co2-monitor/monitor'
MONITOR_ARGS = ['-l']


def get_data():
    call = [MONITOR_BIN] + MONITOR_ARGS
    proc = subprocess.Popen(call, stdout=subprocess.PIPE)
    ret = proc.stdout.readline()
    co2 = ret.split()[1]
    ret = proc.stdout.readline()
    temp = ret.split()[1]
    return int(co2), float(temp)


client = mqtt.Client()
client.connect(MQTT_HOST)

while True:
    # Create JSON object containing CO2 value
    json_data = {}
    co2, temp = get_data()
    json_data['co2'] = co2
    json_data['temperature'] = temp
    json_text = json.dumps(json_data)

    # Publish the value to the corresponding topic on the broker
    client.publish(MQTT_TOPIC_CO2, json_text)

    # Wait for the configured interval
    time.sleep(POLL_INTERVAL)
