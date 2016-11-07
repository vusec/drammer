#!/usr/bin/python

import sys

if len(sys.argv) != 2:
    print "Usage: %s <input>" % sys.argv[0]
    sys.exit(0)

infile = sys.argv[1]
f = open(infile)
for line in f:
    if line.startswith("[RS]"): 
        continue
    deltas = line.split()
   
    conflicts = []
    noconflicts = []
    subset = []
    deltas_printed = 0
    phys = 262144 
    print "%20s:" % (bin(phys)),
    for delta in deltas:
        if deltas_printed < 20:
            print "%2d" % (int(delta)/10),
            subset.append(delta)
        if deltas_printed == 20:
            print "...",
            conflict = True
            for x in subset:
                if int(x) < 100:
                    conflict = False
                    break
            subset = []
            if conflict == True:
                conflicts.append(phys)
            else:
                noconflicts.append(phys)

        deltas_printed += 1

        if deltas_printed == 128:
            phys += 128
            print ""
            print "%20s:" % (bin(phys >> 0)),
            deltas_printed = 0


    print ""
    print "conflicts:"
    conflicts.append(0)
    for conflict in conflicts:
        print "%20s" % (bin(conflict >> 0))
