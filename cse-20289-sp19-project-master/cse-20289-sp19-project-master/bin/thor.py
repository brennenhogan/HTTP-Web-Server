#!/usr/bin/env python3

import multiprocessing
import os
import requests
import sys
import time

# Globals

PROCESSES = 1
REQUESTS  = 1
VERBOSE   = False
URL       = None
SUM       = 0

# Functions

def usage(status=0):
    print('''Usage: {} [-p PROCESSES -r REQUESTS -v] URL
    -h              Display help message
    -v              Display verbose output

    -p  PROCESSES   Number of processes to utilize (1)
    -r  REQUESTS    Number of requests per process (1)
    '''.format(os.path.basename(sys.argv[0])))
    sys.exit(status)

def do_request(pid):
    ''' Perform REQUESTS HTTP requests and return the average elapsed time. '''
    SUM = 0

    for i in range(0, REQUESTS):
        start = time.time()
        response = requests.get(URL)
        end = time.time()
        
        if VERBOSE:
            print(response.text)

        elapsed = end - start
        SUM = elapsed + SUM
        print("Process: {}, Request: {}, Elapsed Time: {:.2f}".format(pid, i, elapsed))
    

    average = float(SUM)/float((REQUESTS))
    print("Process: {}, AVERAGE   , Elapsed Time: {:.2f}".format(pid, average))

    return average

# Main execution

if __name__ == '__main__':
    # Parse command line arguments
    args = sys.argv[1:]

    if not len(args):
        usage(1)

    while len(args) and len(args[0]) > 1:
        arg = args.pop(0)
        if arg == '-h':
            usage(0)
        elif arg == '-v':
            VERBOSE = 1
        elif arg == '-p':
            PROCESSES = int(args.pop(0))
        elif arg == '-r':
            REQUESTS = int(args.pop(0))
        elif arg.startswith('http'):
            URL = arg
        else:
            usage(1)


    # Create pool of workers and perform requests
   
    if PROCESSES > 1:
        processNums = range(0, PROCESSES)
        pool = multiprocessing.Pool(PROCESSES)
        average = pool.map(do_request, processNums)

        average = float(sum(average)) / float((PROCESSES)) 
    else:
        average = do_request(0)

    print("TOTAL AVERAGE ELAPSED TIME: {:.2f}".format(average))

# vim: set sts=4 sw=4 ts=8 expandtab ft=python:
