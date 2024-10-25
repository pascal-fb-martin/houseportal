#!/bin/bash
exec top -w 200 -b -n 1 -o RES $* | grep '\(^[ ]*PID USER\)\|\( house[a-z]\)\|\( orvibo\)\|\( waterwise\)'

