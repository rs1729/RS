
import numpy as np
import matplotlib.pyplot as plt


# { b: blue , g: green , r: red , c: cyan , m: magenta , y: yellow , k: black , w: white }

data = np.genfromtxt( 'db.txt', delimiter=';', names=['1','2','3','4','5','6','7','8', '9'] )

fq = data['1']
freq = data['2']
db = data['3']
intdb = data['4']
avg = data['5']
dev = data['6']
peak = data['7']
peak2 = data['8']
mag = data['9']


ax1 = plt.subplot(211)
ax1.plot( fq, db, color='b', linewidth=0.2, label='fft')
ax1.plot( fq, intdb, color='g', label='fft')

ax2 = plt.subplot(212)
ax2.plot( fq, avg, color='b', label='fft')
ax2.plot( fq, peak, color='r', label='fft')
ax2.plot( fq, peak2, color='c', label='fft')
ax2.plot( fq, mag, color='y', label='fft')

plt.show()

