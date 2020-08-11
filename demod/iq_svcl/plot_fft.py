
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


if len(sys.argv) < 2:
    print("usage:")
    print("\tpython %s <fft_file>" % sys.argv[0])
    sys.exit()

fft_file = sys.argv[1]

if not os.path.isfile(fft_file):
    print("error: %s not found" % fft_file)
    sys.exit()


data = np.genfromtxt( fft_file, delimiter=';', names=['fq','db'] , skip_header=1 )

fq   = data['fq']
db   = data['db']


ax1 = plt.subplot(111)
ax1.set_xlim(fq[0], fq[-1])
ax1.plot( fq, db, color='b', linewidth=0.2, label='fft')

plt.show()

