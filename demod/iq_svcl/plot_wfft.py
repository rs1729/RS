
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


if len(sys.argv) < 2:
    print("usage:")
    print("\tpython %s <wfft_file>" % sys.argv[0])
    sys.exit()

fft_file = sys.argv[1]

if not os.path.isfile(fft_file):
    print("error: %s not found" % fft_file)
    sys.exit()


raw_data = np.genfromtxt( fft_file, delimiter=',')
data = raw_data[:,5:]

dim = np.shape(data)
m = np.mean(data)
data5 = np.full(dim, m)
for y in range(dim[0]):
    for x in range(2, dim[1]-2):
        data5[y,x] = (data[y,x-2]+data[y,x-1]+data[y,x]+data[y,x+1]+data[y,x+2])/5.0

freq_min = raw_data[0,1] / 1e3
freq_max = raw_data[0,2] / 1e3
N = raw_data[0,4]
tmin = raw_data[dim[0]-1,0]
tmax = raw_data[0,0]
limits = [freq_min, freq_max, tmin, tmax]


fig = plt.figure(figsize=(15, 5))
ax = fig.add_subplot(111)
ax.set_title('Waterfall')
ax.set_xlabel('Frequency (kHz)')
ax.set_ylabel('Time (s)')

plt.imshow(data5, extent=limits)

ax.set_aspect('auto')
plt.colorbar(orientation='vertical')
plt.show()



