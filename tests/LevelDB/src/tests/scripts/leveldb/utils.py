import subprocess
import os


def StartBackgroundProcess(boot_command, log_file, append=False, env=dict()):
    print(boot_command)
    print(log_file)
    log = None
    if append:
        log = open(log_file, "a+")
    else:
        log = open(log_file, "w+")
    process = subprocess.Popen(
        boot_command,
        stdout=log,
        stderr=log,
        start_new_session=True,
        env=dict(os.environ) | env,
    )
    if process.poll() is not None:
        print(f"The process ended with return code {process.returncode}")
        exit(1)
    else:
        print("The process is still running")
