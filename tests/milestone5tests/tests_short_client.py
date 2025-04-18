from subprocess import CalledProcessError, STDOUT, check_output, TimeoutExpired, Popen, PIPE, getstatusoutput
import os
import random 
import shutil
import pty
import datetime
import sys
sys.path.append("..")
from time import sleep 
import subprocess
import multiprocessing 
from tests_helpers import *
import socketserver
import socket 


def get_free_port():
    with socketserver.TCPServer(("localhost", 0), None) as s:
        free_port = s.server_address[1]
    sleep(0.2)
    return free_port

# Return True if the port is used, otherwise False. 
def port_running(port, process):
    output = getstatusoutput(f'lsof -i -P -n | grep {process.pid}')[1]
    return str(f":{port}") in output
 

def _test_launch(comment_file_path,student_dir, command_wait=0.2):
  start_test(comment_file_path, "Able to launch and close a server correctly")
  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)
    write_no_stdout_flush(p,"close-server")
    sleep(command_wait)
    write_no_stdout_flush(p,"exit")
    sleep(command_wait)
    if read_stderr(p) != '':
      finish(comment_file_path, "NOT OK")    
      return 
    finish(comment_file_path, "OK")    
  except Exception as e:
    finish(comment_file_path, "NOT OK")  

def _test_port(comment_file_path,student_dir, command_wait=0.2):
  start_test(comment_file_path, "Server reports an error when launched without a port")

  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    write_no_stdout_flush(p,"start-server")
    sleep(command_wait)
    output = read_stderr(p)
    if "ERROR" in output:
        finish(comment_file_path, "OK")   
    else:
        finish(comment_file_path, "NOT OK")   

  except Exception as e:
    finish(comment_file_path, "NOT OK")      


def _test_shutdown(comment_file_path,student_dir, command_wait=0.2):
  start_test(comment_file_path, "Server shuts down after shell exits")
  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)
    write_no_stdout_flush(p,"exit")
    sleep(command_wait)
    if port_running(free_port, p):
      finish(comment_file_path, "NOT OK")        
      return 
    else:
      finish(comment_file_path, "OK")
      return 

  except Exception as e:
    finish(comment_file_path, "NOT OK")

def _test_occupied_port(comment_file_path,student_dir, command_wait=0.2):
  start_test(comment_file_path, "An error when on an occupied port")

  try:
    p = start_not_blocking('./mysh')
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)
    p2 = start_not_blocking('./mysh')
    write_no_stdout_flush(p2,"start-server {}".format(free_port))
    sleep(command_wait)
    error1 = read_stderr(p2)    # There is a reported error message
    error2 = read_stderr(p2)
    if "ERROR" not in error1:
      finish(comment_file_path, "NOT OK")
      return 
    elif "ERROR: Builtin failed: start-server" not in error2:
      finish(comment_file_path, "NOT OK")
      return 

    # Can still execute commands
    write_no_stdout_flush(p2, "echo hello")
    sleep(command_wait)
    output = read_stdout(p2)
    if "hello" in output:
      finish(comment_file_path, "OK")   
    else:
      finish(comment_file_path, "NOT OK")   

  except Exception as e:
    logger(e)
    finish(comment_file_path, "NOT OK")


def _test_message(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=100):
  start_test(comment_file_path, "A shell can exchange a message with itself through a socket")

  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    hostname = "127.0.0.1"
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)
    write_no_stdout_flush(p,"send {} {} testing message".format(free_port, hostname))
    sleep(command_wait)
    output = read_stdout(p)
    write_no_stdout_flush(p,"exit")
    if len(output) < LENGTH_CUTOFF and "testing message" in output:
      finish(comment_file_path, "OK") 
    else:
      finish(comment_file_path, "NOT OK") 
  except Exception as e:
    finish(comment_file_path, "NOT OK")


def _test_spaces(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=100):
  start_test(comment_file_path, "Shell messages ignore extra spaces")

  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    hostname = "127.0.0.1"
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)
    write_no_stdout_flush(p,"send {} {} testing   message".format(free_port, hostname))
    sleep(command_wait)
    output = read_stdout(p)
    write_no_stdout_flush(p,"exit")
    if len(output) < LENGTH_CUTOFF and "testing message" in output:
      finish(comment_file_path, "OK") 
    else:
      finish(comment_file_path, "NOT OK") 
  except Exception as e:
    finish(comment_file_path, "NOT OK")

def _test_variables(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=100):
  start_test(comment_file_path, "Simple shell messages expand variables")

  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    write_no_stdout_flush(p, "hostname=127.0.0.1")
    sleep(command_wait)
    free_port = get_free_port()
    write_no_stdout_flush(p, "port={}".format(free_port))
    sleep(command_wait)
    write_no_stdout_flush(p,"start-server $port")
    sleep(command_wait)
    write_no_stdout_flush(p,"message=abcdef")
    sleep(command_wait)
    write_no_stdout_flush(p,"send $port $hostname $message")
    sleep(command_wait)
    output = read_stdout(p)
    logger("var output:", output)
    write_no_stdout_flush(p,"exit")
    if len(output) < LENGTH_CUTOFF and "abcdef" in output:
      finish(comment_file_path, "OK") 
    else:
      finish(comment_file_path, "NOT OK") 
  except Exception as e:
    finish(comment_file_path, "NOT OK")


def _test_two_shells(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=100):
  start_test(comment_file_path, "Two separate shells can exchange a message")

  try:
    p = start_not_blocking('./mysh')
    hostname = "127.0.0.1"
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)

    p2 = start_not_blocking('./mysh')
    write_no_stdout_flush(p2, "send {} {} testing message".format(free_port, hostname))
    sleep(command_wait)
    # logger("ignored: ", read_stdout(p))

    if "testing message" in (output:=read_stdout(p)):
      finish(comment_file_path, "OK")
    else:
      finish(comment_file_path, "NOT OK")
    logger("output:", output)
  except Exception as e:
    logger(e)
    finish(comment_file_path, "NOT OK")


def _test_ten_shells(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=100):
  start_test(comment_file_path, "Ten separate shells can send messages")

  try:
    p = start_not_blocking('./mysh')
    hostname = "127.0.0.1"
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))


    for i in range(10):
      client = start_not_blocking('./mysh')
      message = "testing message{}".format(i)
      write_no_stdout_flush(client, "send {} {} {}".format(free_port, hostname, message))
      sleep(command_wait)
      write_no_stdout_flush(client, "exit")
      sleep(command_wait)
    sleep(1)
    for i in range(10):
      # read_stdout(p)
      expected_message = "testing message{}".format(i)
      output = ""
      while output == "":
          output = read_stdout(p)
          logger("output 10:", output)
          output = output.replace("mysh$ ", "")
          output = output.replace("mysh$", "")
          sleep(0.2)
      if expected_message not in output or len(expected_message) > LENGTH_CUTOFF:
        logger("output", i ,output, "expected", expected_message)
        finish(comment_file_path, "NOT OK")
        return
    
    finish(comment_file_path, "OK")

  except Exception as e:
    finish(comment_file_path, "NOT OK")


def _test_pipes(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=100):
  start_test(comment_file_path, "Pipes work while a server is running")

  try:
    p = Popen(['./mysh'], stdout=PIPE, stderr=PIPE, stdin=PIPE)
    free_port = get_free_port()
    write_no_stdout_flush(p,"start-server {}".format(free_port))
    sleep(command_wait)
    write_no_stdout_flush(p, "date | cat")
    read_stderr(p)
    finish(comment_file_path, "NOT OK")
  except Exception as e:
    finish(comment_file_path, "NOT OK")
    


def test_short_client_suite(comment_file_path, student_dir):
  start_suite(comment_file_path, "Server commands are setup correctly")
  start_with_timeout(_test_launch, comment_file_path, student_dir)
  start_with_timeout(_test_port, comment_file_path, student_dir)
  start_with_timeout(_test_shutdown, comment_file_path, student_dir, timeout=5)   # Increased timeout for busier conditions
  end_suite(comment_file_path)

  start_suite(comment_file_path, "Sample send runs")
  start_with_timeout(_test_message, comment_file_path, student_dir)
  start_with_timeout(_test_two_shells, comment_file_path, student_dir)
  end_suite(comment_file_path)

  start_suite(comment_file_path, "Send edge cases are handled correctly")
  start_with_timeout(_test_occupied_port, comment_file_path, student_dir)
  start_with_timeout(_test_spaces, comment_file_path, student_dir)
  start_with_timeout(_test_variables, comment_file_path, student_dir)
  end_suite(comment_file_path)

  start_suite(comment_file_path, "Advanced tests")
  start_with_timeout(_test_ten_shells, comment_file_path, student_dir, timeout=10)
  start_with_timeout(_test_pipes, comment_file_path, student_dir, timeoutFeedback="OK\n", timeout=2)
  end_suite(comment_file_path)

