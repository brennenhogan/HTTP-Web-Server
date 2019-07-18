#!/bin/sh


PORT=9977


time ./bin/thor.py -p 5 -r 5 http://student04.cse.nd.edu:$PORT 
#time ./bin/thor.py -p 5 -r 5 http://student04.cse.nd.edu:$PORT/bigwords.txt
#time ./bin/thor.py -p 5 -r 5 http://student04.cse.nd.edu:$PORT/scripts/cowsay.sh?message=hi&template=dragon
