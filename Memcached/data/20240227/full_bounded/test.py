import time
import bmemcached
import sys

client = bmemcached.Client(('memcached:11211'))
# client.set('key', sys.argv[1])
# time.sleep(10)
print(client.set('5', 'value5'))
print(client.set('6', 'value6'))
print(client.get('5'))
print(client.get_multi(['5', '6']))
print(client.get_multi(['5', '6', '7']))
