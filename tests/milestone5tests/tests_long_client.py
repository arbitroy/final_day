import re
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
    return free_port

# Return True if the port is used, otherwise False. 
def port_running(port):
    output = getstatusoutput('lsof | grep mysh')[1]
    return str(port) in output
 

def _messages_exchange_attempt(messages, command_wait=0.2, LENGTH_CUTOFF=100):
    try:
        p = start_not_blocking('./mysh')
        p2 = start_not_blocking('./mysh')
        hostname = "127.0.0.1"
        free_port = get_free_port()
        write_no_stdout_flush(p,"start-server {}".format(free_port))
        sleep(command_wait)
        write_no_stdout_flush(p2,"start-client {} {}".format(free_port, hostname))
        sleep(command_wait)
        for message in messages:
            write_no_stdout_flush(p2,message)
            sleep(command_wait)
        for message in messages:
            output = ""
            while output == "":    
                output = read_stdout(p)   
                # Taking out the mysh$ message for this test so that any blank lines before are accepted. 
                logger("exchange", output)
                output = output.replace("mysh$ ", "")
                output = output.replace("mysh$", "")
                sleep(command_wait)
            if message not in output or len(output) >= LENGTH_CUTOFF:
                return "NOT OK"
            sleep(command_wait)
        
        return "OK"
    except Exception as e:
        return "NOT OK"


def _test_message(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=50):
  start_test(comment_file_path, "A shell can exchange a message with another shell through start-client")
#   result = _messages_exchange_attempt(["examplemessage1"], command_wait, LENGTH_CUTOFF)
  result = _messages_exchange_attempt(["examplemessage1"], command_wait, LENGTH_CUTOFF)
  finish(comment_file_path, result)


def _test_message_v2(comment_file_path,student_dir, command_wait=0.2, LENGTH_CUTOFF=50):
  start_test(comment_file_path, "A shell can exchange a message with another shell through start-client v2")
  result = _messages_exchange_attempt(["123456789"], command_wait, LENGTH_CUTOFF)
  finish(comment_file_path, result)


# Multiple Messages
def _test_multiple_messages(comment_file_path, student_dir, command_wait=0.2, LENGTH_CUTOFF=50):
    start_test(comment_file_path, "A client launched with start-client can send multiple messages")
    test1 = _messages_exchange_attempt(["msg1", "msg2", "msg3"], command_wait, LENGTH_CUTOFF)
    test2 = _messages_exchange_attempt(["a", "b", "c"], command_wait, LENGTH_CUTOFF)
    test3 = _messages_exchange_attempt(["3245", "572", "476", "3", "4"], command_wait, LENGTH_CUTOFF)
    if test1 == "OK" and test2 == "OK" and test3 == "OK": 
        finish(comment_file_path, "OK")
    else:
        finish(comment_file_path, "NOT OK")


# Multiple clients

def _test_multi_clients(comment_file_path, student_dir, command_wait=0.2, LENGTH_CUTOFF=50):
    start_test(comment_file_path, "Multiple clients single message")
    try:
        p = start_not_blocking('./mysh')
        p2 = start_not_blocking('./mysh')
        p3 = start_not_blocking('./mysh')
        hostname = "127.0.0.1"
        free_port = get_free_port()
        write_no_stdout_flush(p,"start-server {}".format(free_port))
        sleep(command_wait)
        write_no_stdout_flush(p2,"start-client {} {}".format(free_port, hostname))
        write_no_stdout_flush(p3,"start-client {} {}".format(free_port, hostname))
        sleep(command_wait)
        write_no_stdout_flush(p2, "message1")
        sleep(command_wait)
        output = ""
        while output == "":
            output = read_stdout(p)
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if not (id1:=re.match("^client\\d+:", output)):
            finish(comment_file_path, "NOT OK -- missing client id")
            return
        
        if "message1" not in output:
            finish(comment_file_path, "NOT OK -- message missing from server")
            return
        
        # make sure all clients recieve the mssage
        output = ""
        while output == "":
            output = read_stdout(p2)
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if "message1" not in output or id1.group() not in output:
            logger("message1 missing in sending client", output)
            finish(comment_file_path, "NOT OK -- message1 missing in sending client")
            return
        output = ""
        while output == "":
            output = read_stdout(p3)
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if "message1" not in output or id1.group() not in output:
            logger("message1 missing in other client", output)
            finish(comment_file_path, "NOT OK -- message1 missing in other client")
            return

        write_no_stdout_flush(p3, "message2")
        sleep(command_wait)
        output = ""
        while output == "":
            output = read_stdout(p) 
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if not (id2:=re.match("^client\\d+:", output)):
            finish(comment_file_path, "NOT OK -- missing client id")
            return
        if id2.group() == id1.group():
            finish(comment_file_path, "NOT OK -- same client id for two clients")
            return
        if "message2" not in output:
            finish(comment_file_path, "NOT OK -- message missing from server")
            return 
        
        # make sure all clients recieve the mssage
        output = ""
        while output == "":
            output = read_stdout(p2)
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if "message2" not in output or id2.group() not in output:
            logger("message2 missing in other client", output)
            finish(comment_file_path, "NOT OK -- message2 missing in other client")
            return
        output = ""
        while output == "":
            output = read_stdout(p3)
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if "message2" not in output or id2.group() not in output:
            logger("message2 missing in sending client", output)
            finish(comment_file_path, "NOT OK -- message2 missing in sending client")
            return
        
        finish(comment_file_path, "OK")
    except Exception as e:
        logger(e)
        finish(comment_file_path, "NOT OK")

def test_connected_command(comment_file_path, student_dir, command_wait=0.2, LENGTH_CUTOFF=50):
    start_test(comment_file_path, "Connected command")
    try:
        p = start_not_blocking('./mysh')
        p2 = start_not_blocking('./mysh')
        p3 = start_not_blocking('./mysh')
        hostname = "127.0.0.1"
        free_port = get_free_port()
        write_no_stdout_flush(p,"start-server {}".format(free_port))
        sleep(command_wait)
        write_no_stdout_flush(p2,"start-client {} {}".format(free_port, hostname))
        write_no_stdout_flush(p3,"start-client {} {}".format(free_port, hostname))
        sleep(command_wait)
        write_no_stdout_flush(p2, "\\connected")
        sleep(command_wait)

        output = ""
        while output == "":
            output = read_stdout(p2)
            output = output.replace("mysh$ ", "")
            output = output.replace("mysh$", "")
        if not '2' in output:
            finish(comment_file_path, "NOT OK")
        else: 
            finish(comment_file_path, "OK")
    except Exception as e:
        finish(comment_file_path, "NOT OK")


def test_client_ids(comment_file_path, student_dir, command_wait=0.2, LENGTH_CUTOFF=50):
    p = start_not_blocking('./mysh')
    p2 = start_not_blocking('./mysh')
    p3 = start_not_blocking('./mysh')


def test_long_client_suite(comment_file_path, student_dir):
  start_suite(comment_file_path, "Long client single message")
  start_with_timeout(_test_message, comment_file_path, student_dir, timeout=5)
  start_with_timeout(_test_message_v2, comment_file_path, student_dir, timeout=5)
  end_suite(comment_file_path)

  start_suite(comment_file_path, "Long client multiple messages")
  start_with_timeout(_test_multiple_messages, comment_file_path, student_dir, timeout=10)
  end_suite(comment_file_path)

  start_suite(comment_file_path, "Multiple clients single message")
  start_with_timeout(_test_multi_clients, comment_file_path, student_dir, timeout=7)
  end_suite(comment_file_path)

  start_suite(comment_file_path, "Connected command")
  start_with_timeout(test_connected_command, comment_file_path, student_dir, timeout=7)
  end_suite(comment_file_path)
