#!/usr/bin/env python3
'''
Debug tests for shell project

This script contains instrumented versions of failing tests to help debug
by showing inputs and outputs.
'''

import os
import sys
import time
import subprocess
from subprocess import Popen, PIPE

# Add test helpers directory to path if needed
sys.path.append('.')
try:
    from tests_helpers import *
except ImportError:
    print("Couldn't import tests_helpers, make sure the file is in the current directory")
    sys.exit(1)

# Set up debug directory
DEBUG_DIR = 'debug_output'
os.makedirs(DEBUG_DIR, exist_ok=True)

# Enable debug mode
enable_debug()

# Debug helper functions
def debug_print(message, level=0):
    indent = '  ' * level
    print(f"\033[94m[DEBUG]\033[0m {indent}{message}")

def log_process_interaction(process, input_text=None, timeout=0.5):
    debug_print(f"Process PID: {process.pid}")
    
    if input_text:
        debug_print(f"Sending: '{input_text}'")
        process.stdin.write(f"{input_text}\n".encode("utf-8"))
        process.stdin.flush()
    
    # Check for output with timeout
    time.sleep(timeout)
    
    stdout_data = b''
    while not stdout_empty(process):
        line = process.stdout.readline()
        stdout_data += line
        debug_print(f"STDOUT: '{line.decode('utf-8').strip()}'")
    
    stderr_data = b''
    while not stderr_empty(process):
        line = process.stderr.readline()
        stderr_data += line
        debug_print(f"STDERR: '{line.decode('utf-8').strip()}'", level=1)
    
    return stdout_data, stderr_data

# Make write function more verbose
def debug_write(process, message):
    debug_print(f"Sending input: '{message}'")
    write(process, message)
    time.sleep(0.2)  # Give process time to respond
    
    # Check for output
    if not stdout_empty(process):
        output = read_stdout(process)
        debug_print(f"Received: '{output}'")
    
    if not stderr_empty(process):
        error = read_stderr(process)
        debug_print(f"Error: '{error}'")

# Instrumented test functions

# Main function to run tests
if __name__ == "__main__":
    print("\033[1mShell Project Debug Tests\033[0m")
    print("Choose a test to run:")
    
    tests = []
    for name, func in sorted(globals().items()):
        if name.startswith('debug_') and callable(func):
            tests.append((name, func))
    
    for i, (name, _) in enumerate(tests):
        print(f"{i+1}. {name}")
    
    choice = input("Enter test number (or 'all' to run all tests): ")
    
    if choice.lower() == 'all':
        for name, func in tests:
            print(f"\n\033[1mRunning {name}...\033[0m")
            try:
                func("debug_results.txt", ".")
                print(f"\033[92mTest completed\033[0m")
            except Exception as e:
                print(f"\033[91mTest failed with error: {e}\033[0m")
    else:
        try:
            idx = int(choice) - 1
            name, func = tests[idx]
            print(f"\n\033[1mRunning {name}...\033[0m")
            func("debug_results.txt", ".")
            print(f"\033[92mTest completed\033[0m")
        except (ValueError, IndexError) as e:
            print(f"\033[91mInvalid choice: {e}\033[0m")
        except Exception as e:
            print(f"\033[91mTest failed with error: {e}\033[0m")
