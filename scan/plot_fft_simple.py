
import numpy as np
import matplotlib.pyplot as plt


# { b: blue , g: green , r: red , c: cyan , m: magenta , y: yellow , k: black , w: white }

data = np.genfromtxt( 'db2.txt', delimiter=';', names=['1','2','3','4','5'] )

fq = data['1']
freq = data['2']
db = data['3']
intdb = data['4']
peak = data['5']

idb_min = np.min(intdb)

ax1 = plt.subplot(111)
ax1.plot( fq, db, color='b', linewidth=0.2, label='fft')
ax1.plot( fq, intdb, color='g', label='fft')
ax1.plot( fq, peak+idb_min, color='r', label='fft')

plt.show()

