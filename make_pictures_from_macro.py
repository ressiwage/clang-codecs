import time
from PIL import Image
import numpy as np
import imageio


with open('out.txt') as f:
    data = f.readline()

index = 0
s=0
e=0

pic=[]
tempx=set()
tempy=set()
prevframe=0
resArr=[[0 for i in range(120)] for _ in range(70)]
images = []
while True:
    s = data.find('(', index)
    e = data.find(')', index)
    
    # print(data[s:e], index, s, e, )
    if index%1000==0:print(round(index/len(data), 2))

    index=e+1
    sub = data[s:e]
    if sub=='':
        break
    # print(sub.replace('(','').replace(')','').split('-'))
    x,y,fn,dc = list(map(int, sub.replace('(','').replace(')','').split(';')))
    # print(x,y)
    resArr[y][x]=dc
    if prevframe!=fn:
        # print(*resArr, sep='\n', end='\n\n')
        img = Image.fromarray( (np.array(resArr) ) + 500 / 1000 * 100 , 'L')
        img.save(f'temp/py{fn}.png')
        images.append(f'temp/py{fn}.png')
        resArr=[[0 for i in range(120)] for _ in range(70)]
        prevframe=fn
    tempx.add(dc)
    # tempy.add(y)
print(tempx, min(tempx), max(tempx))
to_concat=[]
writer = imageio.get_writer('test.mp4', fps=30)
for filename in images:
    writer.append_data(imageio.imread(filename))
writer.close()
