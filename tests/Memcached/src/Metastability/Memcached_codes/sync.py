import bmemcached
import concurrent.futures
import time
import threading

def sync():
  print("Sync started")

  time.sleep(1)
  start_time = time.time()
  warm_up_size = 140000
  # warm_up_size /= 4

  # thread_local = threading.local()
  # def get_clients():
  #     # If the clients don't exist in the current thread, create them
  #     if not hasattr(thread_local, "full"):
  #         thread_local.full = bmemcached.Client(('192.168.254.10:60002'))
  #     if not hasattr(thread_local, "back_up"):
  #         thread_local.back_up = bmemcached.Client(('10.0.233.7:59999'))
  #     return thread_local.full, thread_local.back_up
  # def task(i):
  #     full, back_up = get_clients()
  #     value = back_up.get(str(i))
  #     if value is not None:
  #         result = full.set(str(i), value, compress_level = 0)
  # with concurrent.futures.ThreadPoolExecutor() as executor:
  #   executor.map(task, range(int(warm_up_size), 1, -1))


  # full = bmemcached.Client(('192.168.254.10:60002'))
  # back_up = bmemcached.Client(('10.0.233.7:59999'))
  # def task(i):
  #     value = back_up.get(str(i))
  #     if value is not None:
  #         result = full.set(str(i), value, compress_level = 0)
  # with concurrent.futures.ThreadPoolExecutor() as executor:
  #   executor.map(task, range(int(warm_up_size), 1, -1))

  full = bmemcached.Client(('192.168.254.10:60002'))
  back_up = bmemcached.Client(('10.0.233.7:59999'))
  for i in range(int(warm_up_size), 1, -1):
    value = back_up.get(str(i))
    if value is not None:
      # print(f"Syncing key {i}: {value[:10]}")
      result = full.set(str(i), value, compress_level = 0)
      # print(f"Synced key {i}: {result}: {full.get(str(i))}")

  print(f"Sync completed in {time.time() - start_time} seconds.")

if __name__ == "__main__":
  sync()