import subprocess

def StartBackgroundProcess(boot_command):
    print(boot_command)
    process = subprocess.Popen(boot_command, start_new_session=True)
    if process.poll() is not None:
        print(f"The process ended with return code {process.returncode}")
        exit(1)
    else:
        print("The process is still running")