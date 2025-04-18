#!/usr/bin/env python3
"""
Test cases for failed tests in the shell project
"""

import subprocess
import time
import os
import signal

def run_command(command, timeout=1.0, wait_after=0.1, pre_commands=None):
    """Run a command in the shell and return stdout and stderr"""
    print(f"\n==== TEST: {command} ====")
    print(f"Expected: See comments in test function")
    
    try:
        p = subprocess.Popen(
            './mysh',
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Run any pre-commands if needed
        if pre_commands:
            for cmd in pre_commands:
                p.stdin.write(f"{cmd}\n")
                p.stdin.flush()
                time.sleep(wait_after)
        
        # Send the main command
        p.stdin.write(f"{command}\n")
        p.stdin.flush()
        
        # Wait specified time
        time.sleep(wait_after)
        
        # Exit the shell
        p.stdin.write("exit\n")
        p.stdin.flush()
        
        # Get output with timeout
        stdout, stderr = p.communicate(timeout=timeout)
        
        print(f"STDOUT:\n{stdout.strip()}")
        if stderr:
            print(f"STDERR:\n{stderr.strip()}")
        
        return stdout, stderr
    except subprocess.TimeoutExpired:
        p.kill()
        print("ERROR: Command timed out")
        return "", ""

def test_pipe_variables():
    """
    Test: Variable declaration in pipes is not reflected
    Expected: Pipe should run but variables in pipe shouldn't affect parent shell
    Status: FAILING (TIMEOUT)
    """
    run_command("x=5 | echo $x")
    
def test_pipe_redefine_variables():
    """
    Test: Variable redefinition in pipes is not reflected
    Expected: Should show the original value (5), not the redefined one (6)
    Status: FAILING (TIMEOUT)
    """
    run_command("x=6 | echo $x", pre_commands=["x=5"])

def test_failing_pipe_chain():
    """
    Test: A failing command does not stop the pipe chain
    Expected: Should show error for cat but still execute echo hello
    Status: FAILING (TIMEOUT)
    """
    run_command("cat nonexistent_file | echo hello")

def test_bg_complete():
    """
    Test: Background process completes with DONE message
    Expected: Should show [1]+ Done sleep 0.5 message
    Status: FAILING
    """
    print("\n==== TEST: Background Process Completion ====")
    print("Expected: Should show [1]+ Done sleep 0.5")
    
    p = subprocess.Popen(
        './mysh',
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    p.stdin.write("sleep 0.5 &\n")
    p.stdin.flush()
    time.sleep(0.1)
    
    # Run another command to force prompt display
    p.stdin.write("echo placeholder\n")
    p.stdin.flush()
    
    # Wait for background process to complete
    time.sleep(1.0)
    
    # Exit
    p.stdin.write("exit\n")
    p.stdin.flush()
    
    stdout, stderr = p.communicate(timeout=2.0)
    print(f"STDOUT:\n{stdout.strip()}")
    if stderr:
        print(f"STDERR:\n{stderr.strip()}")

def test_bg_reset():
    """
    Test: Background process counts reset after completion
    Expected: After two processes complete, the next one should be [1] not [3]
    Status: FAILING
    """
    print("\n==== TEST: Background Process Counter Reset ====")
    print("Expected: After short sleeps complete, new bg process should be [1]")
    
    p = subprocess.Popen(
        './mysh',
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Start two short background processes
    p.stdin.write("sleep 0.5 &\n")
    p.stdin.flush()
    time.sleep(0.1)
    
    p.stdin.write("sleep 0.5 &\n")
    p.stdin.flush()
    time.sleep(0.1)
    
    # Wait for them to complete
    time.sleep(1.0)
    
    # Force display of done messages
    p.stdin.write("echo placeholder\n")
    p.stdin.flush()
    time.sleep(0.1)
    
    # Start another process - should be [1] not [3]
    p.stdin.write("sleep 0.5 &\n")
    p.stdin.flush()
    time.sleep(0.1)
    
    # Exit
    p.stdin.write("exit\n")
    p.stdin.flush()
    
    stdout, stderr = p.communicate(timeout=2.0)
    print(f"STDOUT:\n{stdout.strip()}")
    if stderr:
        print(f"STDERR:\n{stderr.strip()}")

def test_simple_message():
    """
    Test: A shell can exchange a message with itself through a socket
    Expected: Should show the message sent to itself
    Status: FAILING
    """
    print("\n==== TEST: Simple Message Exchange ====")
    print("Expected: Shell should exchange message with itself")
    
    # Try to find a free port
    for port in range(8000, 8100):
        p = subprocess.Popen(
            './mysh',
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        p.stdin.write(f"start-server {port}\n")
        p.stdin.flush()
        time.sleep(0.2)
        
        p.stdin.write(f"send {port} 127.0.0.1 test_message\n")
        p.stdin.flush()
        time.sleep(0.5)
        
        p.stdin.write("close-server\n")
        p.stdin.flush()
        time.sleep(0.2)
        
        p.stdin.write("exit\n")
        p.stdin.flush()
        
        stdout, stderr = p.communicate(timeout=2.0)
        print(f"STDOUT:\n{stdout.strip()}")
        if stderr:
            print(f"STDERR:\n{stderr.strip()}")
        
        # If no major errors, don't try more ports
        if "ERROR:" not in stderr:
            break

if __name__ == "__main__":
    # Run tests for pipe variables
    test_pipe_variables()
    test_pipe_redefine_variables()
    test_failing_pipe_chain()
    
    # Run tests for background processes
    test_bg_complete()
    test_bg_reset()
    
    # Run test for messaging
    test_simple_message()