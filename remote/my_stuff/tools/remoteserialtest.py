import subprocess
import time

# still trying to get it running using a "persistent" SSH session
def send_multiple(ssh_host, remote_serial, command, cooldown=0.5, timeout=2.0):
    proc = subprocess.Popen(
        ["ssh", ssh_host, f"nc -q 1 {remote_serial} 20000"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    # Send command
    proc.stdin.write(command + "\n")
    proc.stdin.flush()
    time.sleep(cooldown)

    # Read response
    output = []
    proc.stdout.flush()
    start = time.time()
    while time.time() - start < timeout:
        line = proc.stdout.readline()
        if not line:
            break
        output.append(line.strip())

    proc.stdin.close()
    proc.terminate()
    proc.wait()

    return "\n".join(output)

def send_single(ssh_host, remote_serial, command):
    run = subprocess.run(["ssh", ssh_host, f"echo {command} | nc -q 1 {remote_serial} 20000"])
    return run.stdout

# Example usage:
if __name__ == "__main__":
    print(send_single("saclay", "m3-10", "help"))
    print(send_single("saclay", "m3-10", "ifconfig"))
