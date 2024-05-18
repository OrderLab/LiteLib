import subprocess

def StartBackgroundProcess(boot_command, log_file):
    print(boot_command)
    print(log_file)
    log = open(log_file, 'w+')
    process = subprocess.Popen(boot_command, stdout=log, stderr=log, start_new_session=True)
    if process.poll() is not None:
        print(f"The process ended with return code {process.returncode}")
        exit(1)
    else:
        print("The process is still running")