#!/usr/bin/env python3
"""
Shell test simulation for failing tests
"""

import subprocess
import time
import os

def run_shell_command(command, timeout=1.0, wait_after=0.1):
    """Run a command in the shell and return the output"""
    print(f"\n[TEST] Running: {command}")
    print(f"[EXPECTED] See below")
    
    try:
        p = subprocess.Popen(
            ['./mysh'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Send the command
        p.stdin.write(f"{command}\n")
        p.stdin.flush()
        
        # Wait a bit
        time.sleep(wait_after)
        
        # Exit the shell
        p.stdin.write("exit\n")
        p.stdin.flush()
        
        # Get output with timeout
        stdout, stderr = p.communicate(timeout=timeout)
        
        # Print results
        print(f"[STDOUT]\n{stdout.strip()}")
        if stderr:
            print(f"[STDERR]\n{stderr.strip()}")
        
        return stdout, stderr
    except subprocess.TimeoutExpired:
        p.kill()
        print("[ERROR] Command timed out")
        return "", ""

def test_variable_access():
    """Test variable access that's timing out"""
    print("\n=== TESTING VARIABLE ACCESS ===")
    run_shell_command("x=anothervalue")
    run_shell_command("echo $x")
    
    # The next test that times out
    print("\n=== TESTING VARIABLE ACCESS (TIMEOUT) ===")
    print("[EXPECTED] Should set variable and echo it without timing out")
    run_shell_command("x=anothervalue", timeout=2.0)
    run_shell_command("echo $x", timeout=2.0)

def test_hundred_variables():
    """Test accessing 100 variables which times out"""
    print("\n=== TESTING 100 VARIABLES (TIMEOUT) ===")
    print("[EXPECTED] Should set and access 100 variables without timing out")
    # Just try first few as an example
    for i in range(5):
        run_shell_command(f"x{i}=var{i}", timeout=2.0)
        run_shell_command(f"echo $x{i}", timeout=2.0)

def test_background_done():
    """Test that background processes show DONE message"""
    print("\n=== TESTING BACKGROUND DONE MESSAGES ===")
    print("[EXPECTED] Should show [1]+ Done sleep 0.5 message after sleep completes")
    
    # Use a longer timeout to make sure we capture the completion
    p = subprocess.Popen(
        ['./mysh'],
        stdin=subprocess.PIPE, 
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    p.stdin.write("sleep 0.5 &\n")
    p.stdin.flush()
    time.sleep(0.1)  # Wait for command to register
    p.stdin.write("echo placeholder\n")  # Trigger another prompt
    p.stdin.flush()
    time.sleep(1.0)  # Wait for background process to complete
    p.stdin.write("exit\n")
    p.stdin.flush()
    
    stdout, stderr = p.communicate(timeout=2.0)
    print(f"[STDOUT]\n{stdout.strip()}")
    if stderr:
        print(f"[STDERR]\n{stderr.strip()}")

def test_pipes_with_variables():
    """Test pipes with variables (timeout issue)"""
    print("\n=== TESTING PIPES WITH VARIABLES ===")
    print("[EXPECTED] Variable declaration in pipes not reflected in parent shell")
    run_shell_command("x=5 | echo $x", timeout=2.0)
    
    print("\n[EXPECTED] Variable redefinition in pipes not reflected in parent shell")
    # Set a variable first
    p = subprocess.Popen(
        ['./mysh'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    p.stdin.write("x=5\n")
    p.stdin.flush()
    time.sleep(0.1)
    p.stdin.write("x=6 | echo $x\n")  # Should show "5", not "6"
    p.stdin.flush()
    time.sleep(0.5)
    p.stdin.write("exit\n")
    p.stdin.flush()
    
    stdout, stderr = p.communicate(timeout=2.0)
    print(f"[STDOUT]\n{stdout.strip()}")
    if stderr:
        print(f"[STDERR]\n{stderr.strip()}")

def test_failing_pipe_chain():
    """Test that a failing command doesn't stop pipe chain"""
    print("\n=== TESTING FAILING COMMAND IN PIPE CHAIN ===")
    print("[EXPECTED] Should show error for cat but still execute echo")
    run_shell_command("cat nonexistent_file | echo hello", timeout=2.0)

def main():
    """Run all tests"""
    print("Running shell tests...")
    
    # Create test file for some tests
    with open("test_file.txt", "w") as f:
        f.write("This is a test file\nWith multiple lines\n")
    
    # Run tests
    test_variable_access()
    test_hundred_variables()
    test_background_done()
    test_pipes_with_variables()
    test_failing_pipe_chain()
    
    # Clean up
    if os.path.exists("test_file.txt"):
        os.remove("test_file.txt")

if __name__ == "__main__":
    main()