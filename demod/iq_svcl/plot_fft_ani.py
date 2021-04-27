
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.animation as animation


if len(sys.argv) < 3:
    print("usage:")
    print("\tpython <1/2/3> %s <fft_all.csv>" % sys.argv[0])
    sys.exit()

OPT_FFT_L = 1
OPT_FFT_W = 2
OPT_FFT_B = 3

OPT_FFT = OPT_FFT_B
if   (sys.argv[1] == '1'):
    OPT_FFT = OPT_FFT_L
elif (sys.argv[1] == '2'):
    OPT_FFT = OPT_FFT_W

fft_file = sys.argv[2]


if (fft_file == "-"):
    f = sys.stdin
else:
    try:
        f = open(fft_file)
    except IOError:
        print("error: open %s" % fft_file)
        sys.exit()


FFT_FPS = 16/2
WIN_SEC = 10
win = WIN_SEC*FFT_FPS

line = f.readline()
#line = f.readline()

row = np.fromstring(line, dtype=float, sep=',')
data = row[5:]

l = len(data)
m = np.mean(data)

data5 = np.full([win,l], m)
for x in range(2, l-2):
    data5[0,x] = (data[x-2]+data[x-1]+data[x]+data[x+1]+data[x+2])/5.0

min_db = np.min(data5)
max_db = np.max(data5)

sr = -2.0*row[1]

freq_min = row[1]
freq_max = row[2]
N = row[4]

limits = [freq_min/1e3, freq_max/1e3, WIN_SEC, 0.0]

fq = np.arange(freq_min/sr, freq_max/sr, 1.0/(row[4]+1))


################################################################################################

if (OPT_FFT == OPT_FFT_L):
    fig = plt.figure(figsize=(12, 5))
    ax1 = fig.add_subplot(111)
    ax1.set_xlim([fq[0], fq[-1]])
    ax1.set_ylim([-110.0, -30.0])

    lp, = ax1.plot( fq, data5[0,:], color='g', linewidth=0.4)

    count = 0

    def animate_lp(i):
        line = f.readline()
        if line:
            row = np.fromstring(line, dtype=float, sep=',')
            data = row[5:]

            global count
            count += 1
            global data5
            #data5 = np.roll(data5, 1, axis=0)
            for x in range(2, l-2):
                data5[0,x] = (data[x-2]+data[x-1]+data[x]+data[x+1]+data[x+2])/5.0

            lp.set_data(fq, data5[0,:])

        return [lp]

    ani = animation.FuncAnimation(fig, animate_lp, interval=10, blit=True)

################################################################################################

elif (OPT_FFT == OPT_FFT_W):
    fig = plt.figure(figsize=(12, 5))
    ax2 = fig.add_subplot(111)
    ax2.set_xlabel('Frequency (kHz)')
    ax2.set_ylabel('Time (s)')

    im = ax2.imshow(data5, vmin=-110.0, vmax=-50.0, extent=limits, animated=True)
    ax2.set_aspect('auto')
    fig.colorbar(im, orientation='vertical')

    count = 0

    def animate_im(i):
        line = f.readline()
        if line:
            row = np.fromstring(line, dtype=float, sep=',')
            data = row[5:]

            global count
            count += 1
            global data5
            data5 = np.roll(data5, 1, axis=0)
            for x in range(2, l-2):
                data5[0,x] = (data[x-2]+data[x-1]+data[x]+data[x+1]+data[x+2])/5.0

            im.set_data(data5)

            # update vmin/vmax
            if (count % win == 0):
                min_db = np.min(data5)
                max_db = np.max(data5)
                im.set_clim(vmin=min_db, vmax=max_db)
        return [im]

    ani = animation.FuncAnimation(fig, animate_im, interval=10, blit=True)

################################################################################################

else:
    fig = plt.figure(figsize=(12, 8))
    ax1 = fig.add_subplot(211)
    ax1.set_xlim([fq[0], fq[-1]])
    ax1.set_ylim([-110.0, -30.0])
    ax2 = fig.add_subplot(212)
    ax2.set_xlabel('Frequency (kHz)')
    ax2.set_ylabel('Time (s)')

    lp, = ax1.plot( fq, data5[0,:], color='g', linewidth=0.4)
    im = ax2.imshow(data5, vmin=-110.0, vmax=-50.0, extent=limits, animated=True)

    ax2.set_aspect('auto')

    count = 0

    def animate(i):
        line = f.readline()
        if line:
            row = np.fromstring(line, dtype=float, sep=',')
            data = row[5:]

            global count
            count += 1
            global data5
            data5 = np.roll(data5, 1, axis=0)
            for x in range(2, l-2):
                data5[0,x] = (data[x-2]+data[x-1]+data[x]+data[x+1]+data[x+2])/5.0

            im.set_data(data5)
            lp.set_data(fq, data5[0,:])

            # update vmin/vmax
            if (count % win == 0):
                min_db = np.min(data5)
                max_db = np.max(data5)
                im.set_clim(vmin=min_db, vmax=max_db)
        return [lp,im]

    ani = animation.FuncAnimation(fig, animate, interval=10, blit=True)

################################################################################################

plt.show()



