#!/usr/bin/env python3
"""
Debug failing tests for shell project

This script parses the FEEDBACK file, extracts failing tests,
and creates debug versions that show input and output to help
identify where things are going wrong.
"""

import os
import re
import sys
import glob
import importlib.util
import inspect
import subprocess
from collections import defaultdict

# Colors for terminal output
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

# Check if a filename was provided
if len(sys.argv) > 1:
    feedback_file = sys.argv[1]
else:
    # Try to find the feedback file in the current directory
    feedback_files = [f for f in os.listdir('.') if f.startswith('FEEDBACK') and f.endswith('.txt')]
    if feedback_files:
        feedback_file = feedback_files[0]
    else:
        print("No FEEDBACK file found. Please provide the path to the FEEDBACK file.")
        sys.exit(1)

print(f"{Colors.HEADER}Analyzing {feedback_file}...{Colors.ENDC}")

# Read the feedback file
with open(feedback_file, 'r') as f:
    feedback_content = f.read()

# Regular expressions to extract test information
suite_pattern = re.compile(r'(.*?):\n((?:  .*?\n)+)Result:(PASS|FAIL)\n', re.MULTILINE)
test_pattern = re.compile(r'  .*?----(.*?):(.*?)\n')

# Process test suites
failing_tests = []

for match in suite_pattern.finditer(feedback_content):
    suite_name = match.group(1)
    tests_content = match.group(2)
    result = match.group(3)
    
    if result == "FAIL":
        for test_match in test_pattern.finditer(tests_content):
            test_name = test_match.group(1)
            test_result = test_match.group(2).strip()
            if test_result != "OK":
                failing_tests.append((suite_name, test_name, test_result))

# Count failure types
failure_types = defaultdict(int)
for _, _, result in failing_tests:
    failure_types[result] += 1

print(f"\n{Colors.BOLD}Found {len(failing_tests)} failing tests{Colors.ENDC}")
print(f"\n{Colors.BOLD}Failure types:{Colors.ENDC}")
for failure_type, count in failure_types.items():
    print(f"  {Colors.RED}{failure_type}{Colors.ENDC}: {count} tests")

# Scan all test files to find the test functions
print(f"\n{Colors.BOLD}Scanning test files...{Colors.ENDC}")
test_files = glob.glob("tests_*.py")

if not test_files:
    print(f"{Colors.RED}No test files found in current directory!{Colors.ENDC}")
    print("Make sure you're running this script from the directory with the test files.")
    sys.exit(1)

# Function to convert a test name to a function name
def test_name_to_func_name(test_name):
    # Convert spaces and special chars to underscores and remove non-alphanumeric
    base_name = re.sub(r'[^a-zA-Z0-9]', '_', test_name.lower())
    # Handle special cases - adjust as needed
    base_name = base_name.replace('v_', 'v')
    return f"_test_{base_name}"

# Attempt to map failing tests to test functions
test_map = {}
function_source_code = {}

for test_file in test_files:
    module_name = os.path.splitext(test_file)[0]
    
    # Load the module
    spec = importlib.util.spec_from_file_location(module_name, test_file)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    
    # Get all test functions
    for name, func in inspect.getmembers(module, inspect.isfunction):
        if name.startswith('_test_'):
            # Store test source code
            function_source_code[name] = inspect.getsource(func)
            
            for suite_name, test_name, _ in failing_tests:
                func_name_guess = test_name_to_func_name(test_name)
                
                # Check for exact match or partial match
                if name == func_name_guess or func_name_guess in name:
                    test_map[(suite_name, test_name)] = (module_name, name, func)
                    break

# Create debug versions of the failing test functions
print(f"\n{Colors.BOLD}Creating debug versions of failing tests...{Colors.ENDC}")

debug_code = """#!/usr/bin/env python3
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
    print(f"\\033[94m[DEBUG]\\033[0m {indent}{message}")

def log_process_interaction(process, input_text=None, timeout=0.5):
    debug_print(f"Process PID: {process.pid}")
    
    if input_text:
        debug_print(f"Sending: '{input_text}'")
        process.stdin.write(f"{input_text}\\n".encode("utf-8"))
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
"""

# Add modified test functions to the debug code
for (suite_name, test_name), (module_name, func_name, func) in test_map.items():
    source = function_source_code[func_name]
    
    # Modify the function to add debug output
    modified_source = source.replace("def _test_", f"def debug_{func_name[6:]}_")
    
    # Add write instrumentation
    modified_source = modified_source.replace("write(", "debug_write(")
    modified_source = modified_source.replace("write_no_stdout_flush(", "debug_write(")
    
    # Add comment with test info
    debug_func = f"""
# {'-' * 70}
# Test: {test_name}
# Suite: {suite_name}
# Original function: {func_name} in {module_name}
# {'-' * 70}
{modified_source}
"""
    debug_code += debug_func

# Add main section to run individual tests
debug_code += """
# Main function to run tests
if __name__ == "__main__":
    print("\\033[1mShell Project Debug Tests\\033[0m")
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
            print(f"\\n\\033[1mRunning {name}...\\033[0m")
            try:
                func("debug_results.txt", ".")
                print(f"\\033[92mTest completed\\033[0m")
            except Exception as e:
                print(f"\\033[91mTest failed with error: {e}\\033[0m")
    else:
        try:
            idx = int(choice) - 1
            name, func = tests[idx]
            print(f"\\n\\033[1mRunning {name}...\\033[0m")
            func("debug_results.txt", ".")
            print(f"\\033[92mTest completed\\033[0m")
        except (ValueError, IndexError) as e:
            print(f"\\033[91mInvalid choice: {e}\\033[0m")
        except Exception as e:
            print(f"\\033[91mTest failed with error: {e}\\033[0m")
"""

# Write the debug code to a file
with open("debug_tests.py", "w") as f:
    f.write(debug_code)

# Make it executable
os.chmod("debug_tests.py", 0o755)

# Summary
print(f"\n{Colors.GREEN}Created debug_tests.py with {len(test_map)} instrumented test functions{Colors.ENDC}")
print(f"{Colors.BOLD}Could not find implementations for {len(failing_tests) - len(test_map)} tests.{Colors.ENDC}")

# List tests in the debug file
print(f"\n{Colors.BOLD}Available tests in debug_tests.py:{Colors.ENDC}")
test_count = 0
for (suite_name, test_name), (module_name, func_name, _) in sorted(test_map.items(), key=lambda x: x[0][0]):
    test_count += 1
    debug_name = f"debug_{func_name[6:]}"
    print(f"{test_count}. {Colors.YELLOW}{debug_name}{Colors.ENDC} - {suite_name}: {test_name}")

print(f"\n{Colors.BOLD}Instructions:{Colors.ENDC}")
print("1. Run the debug script: python3 debug_tests.py")
print("2. Choose a test to run and observe the detailed input/output")
print("3. Look for specific error messages or unexpected behaviors")
print("4. Compare the debug output to your shell implementation to find issues")

# Look for common patterns in the failing tests
if "NOT OK (TIMEOUT)" in str(failure_types):
    print(f"\n{Colors.BOLD}Common Issue Detected:{Colors.ENDC} Many timeout failures")
    print("This usually means your shell isn't returning control properly.")
    print("Check for:")
    print("- Infinite loops in your code")
    print("- Processes not being properly terminated")
    print("- File descriptors not being closed")
    print("- Reading from stdin when there's no more input")