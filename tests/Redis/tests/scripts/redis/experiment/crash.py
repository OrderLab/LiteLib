import utils
import os
import signal


def main():
    full = utils.get_process("redis-server", 6379)
    if full is not None:
        os.kill(full.pid, signal.SIGINT)
