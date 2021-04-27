
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


if len(sys.argv) < 2:
    print("usage:")
    print("\tpython %s <fft_file.csv>" % sys.argv[0])
    sys.exit()

fft_file = sys.argv[1]

if not os.path.isfile(fft_file):
    print("error: %s not found" % fft_file)
    sys.exit()


raw_data = np.genfromtxt( fft_file, delimiter=',', max_rows=1)
data1 = raw_data[:] # max_rows=2: raw_data[0,:]
print(data1)

db = data1[5:]

sr = -2.0*data1[1]

freq_min = data1[1] / sr
freq_max = data1[2] / sr
fq   = np.arange(freq_min, freq_max, 1.0/(data1[4]+1))

N = len(db)
m = np.mean(db)

db5 = np.full(N, m)
for n in range(2, N-2):
    db5[n] = (db[n-2]+db[n-1]+db[n]+db[n+1]+db[n+2])/5.0


ax1 = plt.subplot(111)
ax1.set_xlim(fq[0], fq[-1])
ax1.plot( fq, db, color='b', linewidth=0.06, label='fft')
ax1.plot( fq, db5, color='g', linewidth=0.4, label='fft')

plt.show()

