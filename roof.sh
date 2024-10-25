#!/bin/bash
exec top -w -b -n 1 -o RES $* | grep '\(^[ ]*PID USER\)\|\( house[a-z]\)\|\( orvibo\)\|\( waterwise\)'

