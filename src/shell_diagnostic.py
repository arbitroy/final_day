#!/usr/bin/env python3
"""
Shell Diagnostic Tool - Test your shell implementation functionality

This script runs targeted tests to diagnose specific issues in your shell
implementation. It's designed to provide more detailed feedback than the
automated test suite.
"""

import subprocess
import os
import time
import socket
import signal
import sys
import re
from threading import Thread

# Colors for terminal output
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
RESET = "\033[0m"

def print_header(text):
    """Print a section header"""
    print(f"\n{BLUE}{'='*80}{RESET}")
    print(f"{BLUE}== {text}{RESET}")
    print(f"{BLUE}{'='*80}{RESET}")

def print_success(text):
    """Print a success message"""
    print(f"{GREEN}✓ {text}{RESET}")

def print_failure(text):
    """Print a failure message"""
    print(f"{RED}✗ {text}{RESET}")

def print_info(text):
    """Print an info message"""
    print(f"{YELLOW}ℹ {text}{RESET}")

def run_test(name, command, expected_output=None, expected_error=None, timeout=5, exact_match=False):
    """Run a command in the shell and check output against expected values"""
    print(f"\n{BLUE}Testing: {name}{RESET}")
    print(f"Command: {command}")
    
    try:
        # Start the shell process
        process = subprocess.Popen(
            ["./mysh"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Send the command
        process.stdin.write(f"{command}\n")
        process.stdin.write("exit\n")  # Exit the shell after command
        process.stdin.flush()
        
        # Get the output with timeout
        stdout, stderr = process.communicate(timeout=timeout)
        
        # Clean output (remove prompt)
        cleaned_stdout = re.sub(r'mysh\$\s*', '', stdout).strip()
        cleaned_stderr = stderr.strip()
        
        # Check output
        success = True
        if expected_output is not None:
            if exact_match and cleaned_stdout != expected_output:
                success = False
                print_failure(f"Output mismatch")
                print(f"  Expected: '{expected_output}'")
                print(f"  Got: '{cleaned_stdout}'")
            elif not exact_match and expected_output not in cleaned_stdout:
                success = False
                print_failure(f"Expected output not found")
                print(f"  Expected to contain: '{expected_output}'")
                print(f"  Got: '{cleaned_stdout}'")
        
        if expected_error is not None:
            if exact_match and cleaned_stderr != expected_error:
                success = False
                print_failure(f"Error mismatch")
                print(f"  Expected: '{expected_error}'")
                print(f"  Got: '{cleaned_stderr}'")
            elif not exact_match and expected_error not in cleaned_stderr:
                success = False
                print_failure(f"Expected error not found")
                print(f"  Expected to contain: '{expected_error}'")
                print(f"  Got: '{cleaned_stderr}'")
        
        if success:
            print_success("Test passed!")
        
        return success, cleaned_stdout, cleaned_stderr
    
    except subprocess.TimeoutExpired:
        process.kill()
        print_failure(f"Command timed out after {timeout} seconds")
        return False, None, None
    except Exception as e:
        print_failure(f"Test failed with exception: {e}")
        return False, None, None

def test_echo_special_chars():
    """Test echo with special characters"""
    print_header("Testing echo with special characters")
    
    special_chars = "@#*%*(*#&(%*&*)*&^%*#@"
    run_test(
        "Echo special characters",
        f"echo {special_chars}",
        expected_output=special_chars
    )

def test_pipes_with_variables():
    """Test pipe behavior with variables"""
    print_header("Testing pipes with variables")
    
    # Test variable assignment in pipe (should not affect parent shell)
    result, stdout, stderr = run_test(
        "Variable assignment in pipe",
        "x=5\nx=6 | echo $x\necho $x",
        timeout=3
    )
    
    # Check if variable value is still the original (5)
    if "5" in stdout and "6" not in stdout:
        print_success("Variable assignment in pipe did not affect parent shell")
    else:
        print_failure("Variable assignment in pipe affected parent shell or not handled correctly")
    
    # Test pipe with failing command
    run_test(
        "Pipe with failing command",
        "cat nonexistentfile | echo still_working",
        expected_output="still_working"
    )

def test_background_processes():
    """Test background process handling"""
    print_header("Testing background processes")
    
    # Test background process notification
    process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Start a short background process
    process.stdin.write("sleep 1 &\n")
    process.stdin.flush()
    
    # Wait for process to complete and capture output
    time.sleep(2)
    
    # Send a command to trigger output display
    process.stdin.write("echo test\n")
    process.stdin.flush()
    
    # Give time for output to be processed
    time.sleep(0.5)
    
    # Exit and get all output
    process.stdin.write("exit\n")
    process.stdin.flush()
    stdout, stderr = process.communicate(timeout=3)
    
    # Check for background job completion message
    if "[1]+ Done sleep 1" in stdout:
        print_success("Background process completion message correctly formatted")
    else:
        print_failure("Background process completion message incorrectly formatted or missing")
        print(f"Got: {stdout}")
    
    # Test job ID reset
    process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Start and complete multiple processes
    process.stdin.write("sleep 0.5 &\n")
    process.stdin.flush()
    time.sleep(0.6)
    
    # Start another to check job ID
    process.stdin.write("sleep 0.5 &\n")
    process.stdin.flush()
    
    # Get output
    time.sleep(0.1)
    process.stdin.write("exit\n")
    process.stdin.flush()
    stdout, stderr = process.communicate(timeout=3)
    
    # Check if job ID reset to 1
    if "[1]" in stdout.splitlines()[-2] or "[1]" in stdout.splitlines()[-3]:
        print_success("Job ID correctly reset to 1 after all processes completed")
    else:
        print_failure("Job ID not reset properly")
        print(f"Got: {stdout}")

def test_network_functionality():
    """Test network functionality"""
    print_header("Testing network functionality")
    
    # Start a server in a separate process
    server_process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    try:
        # Find a free port
        temp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        temp_sock.bind(('', 0))
        port = temp_sock.getsockname()[1]
        temp_sock.close()
        
        print_info(f"Using port {port} for testing")
        
        # Start server
        server_process.stdin.write(f"start-server {port}\n")
        server_process.stdin.flush()
        time.sleep(1)  # Give time for server to start
        
        # Send a message
        client_process = subprocess.Popen(
            ["./mysh"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        client_process.stdin.write(f"send {port} 127.0.0.1 test_message\n")
        client_process.stdin.flush()
        time.sleep(0.5)
        
        # Check if message was received by server
        client_process.stdin.write("exit\n")
        client_process.stdin.flush()
        client_stdout, client_stderr = client_process.communicate(timeout=2)
        
        # Get server output
        server_output = ""
        while server_process.stdout.readable() and not server_process.stdout.closed:
            line = server_process.stdout.readline()
            if not line:
                break
            server_output += line
            if "test_message" in line:
                break
            
        # Check if message was sent correctly
        if "test_message" in client_stdout or "test_message" in server_output:
            print_success("Message successfully sent and received")
        else:
            print_failure("Message not sent or received correctly")
            print(f"Client output: {client_stdout}")
            print(f"Server partial output: {server_output}")
        
        # Clean up
        server_process.stdin.write("close-server\n")
        server_process.stdin.flush()
        time.sleep(0.5)
        server_process.stdin.write("exit\n")
        server_process.stdin.flush()
        server_process.communicate(timeout=2)
        
    except Exception as e:
        print_failure(f"Network test failed with exception: {e}")
    finally:
        # Make sure processes are terminated
        if server_process.poll() is None:
            server_process.terminate()

def analyze_pipe_behavior():
    """Analyze how pipe errors are handled"""
    print_header("Analyzing pipe error handling")
    
    # Test pipe error propagation
    process = subprocess.Popen(
        ["./mysh"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Run a pipeline with a failing command
    command = "cat nonexistentfile | echo still_works"
    process.stdin.write(f"{command}\n")
    process.stdin.write("exit\n")
    process.stdin.flush()
    
    stdout, stderr = process.communicate(timeout=3)
    
    # Check if second command still executed
    if "still_works" in stdout:
        print_success("Pipeline continues execution after command failure")
    else:
        print_failure("Pipeline fails to continue after command failure")
        print(f"Output: {stdout}")
        print(f"Error: {stderr}")

def analyze_socket_message_format():
    """Analyze message formatting in network communication"""
    print_header("Analyzing network message formatting")
    
    # This is a more detailed test that requires manual verification
    print_info("This test requires manual verification.")
    print_info("1. Run './mysh' in two separate terminals")
    print_info("2. In first terminal: start-server 12345")
    print_info("3. In second terminal: start-client 12345 127.0.0.1")
    print_info("4. In client terminal, type: test_message")
    print_info("5. Verify the message format in the server terminal")
    print_info("6. Expected format: 'client#1: test_message'")
    print_info("7. Close both shells when done")

if __name__ == "__main__":
    print_header("Shell Diagnostic Tool")
    print("This tool will run targeted tests to help diagnose issues with your shell implementation.")
    
    try:
        # Check if shell exists and can be executed
        if not os.path.exists("./mysh"):
            print_failure("Shell executable 'mysh' not found in current directory")
            print_info("Make sure to compile your shell first with 'make'")
            sys.exit(1)
        
        # Run tests
        test_echo_special_chars()
        test_pipes_with_variables()
        test_background_processes()
        analyze_pipe_behavior()
        
        # Network tests are more complex
        print_info("\nWould you like to run network tests? (y/n)")
        response = input()
        if response.lower() == 'y':
            test_network_functionality()
            analyze_socket_message_format()
        
        print_header("Diagnostic Complete")
        print_info("Review the results above to identify issues in your implementation.")
        print_info("Focus on failures and check the exact format expected by the tests.")
        
    except KeyboardInterrupt:
        print("\nDiagnostic tool interrupted by user")
        sys.exit(0)