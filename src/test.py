#!/usr/bin/env python3
"""
Shell Debug Program

This program runs specific commands through your shell and captures detailed output,
error messages, and exit codes to help diagnose issues with the shell implementation.
"""

import os
import subprocess
import fcntl
import time
import sys

def set_nonblocking(file_obj):
    """Set a file object to non-blocking mode"""
    flags = fcntl.fcntl(file_obj, fcntl.F_GETFL)
    fcntl.fcntl(file_obj, fcntl.F_SETFL, flags | os.O_NONBLOCK)

def read_all_output(file_obj, timeout=0.5):
    """Read all available output from a file object with timeout"""
    start_time = time.time()
    output = b""
    
    while time.time() - start_time < timeout:
        try:
            chunk = file_obj.read(4096)
            if chunk is None or chunk == b'':
                break
            output += chunk
        except (IOError, BlockingIOError):
            # No data available yet
            time.sleep(0.01)
            continue
    
    return output

def run_command(command, shell_path="./mysh", timeout=2.0):
    """Run a command through the shell and capture all output"""
    print(f"\n{'=' * 50}")
    print(f"Testing command: {command}")
    print(f"{'=' * 50}")
    
    # Start the shell process
    process = subprocess.Popen(
        [shell_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=False  # Use binary mode
    )
    
    # Set stdout and stderr to non-blocking
    set_nonblocking(process.stdout)
    set_nonblocking(process.stderr)
    
    # Wait for shell to start and display prompt
    time.sleep(0.2)
    
    # Read initial prompt
    initial_output = read_all_output(process.stdout).decode('utf-8', errors='replace')
    print(f"Initial prompt: '{initial_output}'")
    
    # Send the command
    print(f"Sending command: '{command}'")
    process.stdin.write(f"{command}\n".encode('utf-8'))
    process.stdin.flush()
    
    # Wait for command to execute
    time.sleep(timeout)
    
    # Read output and error
    command_output = read_all_output(process.stdout).decode('utf-8', errors='replace')
    error_output = read_all_output(process.stderr).decode('utf-8', errors='replace')
    
    # Send exit command to terminate shell
    print("Sending exit command")
    process.stdin.write(b"exit\n")
    process.stdin.flush()
    
    # Wait for shell to exit
    try:
        exit_code = process.wait(timeout=1.0)
    except subprocess.TimeoutExpired:
        process.kill()
        exit_code = -1
    
    # Display results
    print("\nResults:")
    print(f"Command output: '{command_output}'")
    print(f"Error output: '{error_output}'")
    print(f"Exit code: {exit_code}")
    
    return command_output, error_output, exit_code

def debug_echo():
    """Debug the echo command"""
    print("\n\n== DEBUGGING ECHO COMMAND ==")
    run_command("echo hello world")
    run_command("echo 'quoted string'")
    run_command("echo multiple     spaces")

def debug_variables():
    """Debug variable assignment and access"""
    print("\n\n== DEBUGGING VARIABLES ==")
    run_command("x=test_value")
    run_command("echo $x")
    
    # Test multiple commands in sequence
    process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    set_nonblocking(process.stdout)
    set_nonblocking(process.stderr)
    
    print("\nTesting variable sequence:")
    
    # Read initial prompt
    time.sleep(0.2)
    initial_output = read_all_output(process.stdout).decode('utf-8', errors='replace')
    print(f"Initial prompt: '{initial_output}'")
    
    # Set variable
    print("Setting variable: var=value123")
    process.stdin.write(b"var=value123\n")
    process.stdin.flush()
    time.sleep(0.5)
    output1 = read_all_output(process.stdout).decode('utf-8', errors='replace')
    error1 = read_all_output(process.stderr).decode('utf-8', errors='replace')
    print(f"Output after set: '{output1}'")
    print(f"Error after set: '{error1}'")
    
    # Access variable
    print("Accessing variable: echo $var")
    process.stdin.write(b"echo $var\n")
    process.stdin.flush()
    time.sleep(0.5)
    output2 = read_all_output(process.stdout).decode('utf-8', errors='replace')
    error2 = read_all_output(process.stderr).decode('utf-8', errors='replace')
    print(f"Output after access: '{output2}'")
    print(f"Error after access: '{error2}'")
    
    # Clean up
    process.stdin.write(b"exit\n")
    process.stdin.flush()
    process.wait(timeout=1.0)

def debug_pipes():
    """Debug pipe functionality"""
    print("\n\n== DEBUGGING PIPES ==")
    run_command("echo hello | cat")
    run_command("echo test | grep test")
    run_command("ls | wc -l")

def debug_background():
    """Debug background processes"""
    print("\n\n== DEBUGGING BACKGROUND PROCESSES ==")
    run_command("sleep 1 &", timeout=1.5)
    
    # Test background with follow-up command
    process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    set_nonblocking(process.stdout)
    set_nonblocking(process.stderr)
    
    print("\nTesting background with follow-up:")
    
    # Read initial prompt
    time.sleep(0.2)
    initial_output = read_all_output(process.stdout).decode('utf-8', errors='replace')
    print(f"Initial prompt: '{initial_output}'")
    
    # Start background process
    print("Starting background process: sleep 0.5 &")
    process.stdin.write(b"sleep 0.5 &\n")
    process.stdin.flush()
    time.sleep(0.5)
    output1 = read_all_output(process.stdout).decode('utf-8', errors='replace')
    error1 = read_all_output(process.stderr).decode('utf-8', errors='replace')
    print(f"Output after background start: '{output1}'")
    print(f"Error after background start: '{error1}'")
    
    # Run follow-up command
    print("Running follow-up command: echo testing")
    process.stdin.write(b"echo testing\n")
    process.stdin.flush()
    time.sleep(1.0)  # Wait for both commands to complete
    output2 = read_all_output(process.stdout).decode('utf-8', errors='replace')
    error2 = read_all_output(process.stderr).decode('utf-8', errors='replace')
    print(f"Output after follow-up: '{output2}'")
    print(f"Error after follow-up: '{error2}'")
    
    # Clean up
    process.stdin.write(b"exit\n")
    process.stdin.flush()
    process.wait(timeout=1.0)

def debug_signal_handling():
    """Debug signal handling"""
    print("\n\n== DEBUGGING SIGNAL HANDLING ==")
    
    # Test SIGINT handling
    process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    set_nonblocking(process.stdout)
    set_nonblocking(process.stderr)
    
    print("\nTesting SIGINT handling:")
    
    # Read initial prompt
    time.sleep(0.2)
    initial_output = read_all_output(process.stdout).decode('utf-8', errors='replace')
    print(f"Initial prompt: '{initial_output}'")
    
    # Send SIGINT
    print("Sending SIGINT signal")
    process.send_signal(2)  # SIGINT
    time.sleep(0.5)
    output1 = read_all_output(process.stdout).decode('utf-8', errors='replace')
    error1 = read_all_output(process.stderr).decode('utf-8', errors='replace')
    print(f"Output after SIGINT: '{output1}'")
    print(f"Error after SIGINT: '{error1}'")
    
    # Check if shell is still responsive
    print("Testing if shell is still responsive: echo after_signal")
    try:
        process.stdin.write(b"echo after_signal\n")
        process.stdin.flush()
        time.sleep(0.5)
        output2 = read_all_output(process.stdout).decode('utf-8', errors='replace')
        error2 = read_all_output(process.stderr).decode('utf-8', errors='replace')
        print(f"Output after test command: '{output2}'")
        print(f"Error after test command: '{error2}'")
    except BrokenPipeError:
        print("ERROR: Shell terminated after SIGINT (broken pipe)")
    
    # Clean up
    try:
        process.stdin.write(b"exit\n")
        process.stdin.flush()
        process.wait(timeout=1.0)
    except:
        pass

def debug_file_descriptors():
    """Debug file descriptor management"""
    print("\n\n== DEBUGGING FILE DESCRIPTOR MANAGEMENT ==")
    
    # Create a test file
    with open("test_file.txt", "w") as f:
        f.write("Line 1\nLine 2\nLine 3\n")
    
    run_command("cat test_file.txt")
    run_command("wc test_file.txt")
    run_command("cat test_file.txt | wc")
    
    # Clean up
    os.remove("test_file.txt")

def main():
    """Main debug function"""
    # Check if shell exists
    if not os.path.exists("./mysh"):
        print("Error: Shell executable './mysh' not found")
        print("Please compile the shell first with 'make'")
        return
    
    print("Shell Debug Program\n")
    print("This program will run various commands through your shell")
    print("and capture detailed output to help diagnose issues.")
    
    # Run debug functions
    debug_echo()
    debug_variables()
    debug_pipes()
    debug_background()
    debug_signal_handling()
    debug_file_descriptors()
    
    print("\n\nDebug tests completed.")

if __name__ == "__main__":
    main()