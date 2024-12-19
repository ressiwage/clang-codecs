import cv2
import os

image_folder = 'temp/'
video_name = 'video.avi'

images = sorted([img for img in os.listdir(image_folder) if img.endswith(".png")], key = lambda x:int(x.split('frame-')[1].split('.')[0]))

frame = cv2.imread(os.path.join(image_folder, images[0]))
height, width, layers = frame.shape

video = cv2.VideoWriter(video_name, 0, 30, (width,height))

for image in images:
    video.write(cv2.imread(os.path.join(image_folder, image)))

cv2.destroyAllWindows()
video.release()