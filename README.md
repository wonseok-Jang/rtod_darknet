# Yolo-v4 and Yolo-v3/v2 for Windows and Linux
### (neural network for object detection) - Tensor Cores can be used on [Linux](https://github.com/AlexeyAB/darknet#how-to-compile-on-linux) and [Windows](https://github.com/AlexeyAB/darknet#how-to-compile-on-windows-using-cmake-gui)

More details
* http://pjreddie.com/darknet/yolo/
* https://github.com/AlexeyAB/darknet

# R-TOD: Real-Time Object Detector

### Installation ###
* Clone R-TOD repository (Submodule: https://github.com/AveesLab/OpenCV-3.3.1)
```
$ git clone --recursive https://github.com/AveesLab/R-TOD
```
* To use **On-demand Capture** with OpenCV, you don't need any modification. Just build it. See **OpenCV rebuild** in https://github.com/AveesLab/OpenCV-3.3.1.

### Compile using 'Make' ###
* `V4L2=1`: Fetch image with On-demand capture method using V4L2 ioctl without OpenCV library (0: Fetch image using OpenCV).
* `ZERO_SLACK=1`: Use Zero-Slack Pipeline method
* `CONTENTION_FREE=1`: Use Contention-Free Pipeline method
* `MEASUREMENT=1`: Measure delay (capture ~ display) and log to csv file (See [Measurement setup](#measurement-setup)).

### How to set On-demand capture
* If you build with `V4L2=0`: See https://github.com/AveesLab/OpenCV-3.3.1 .
* If you build with `V4L2=1`: No setup required.

### Measurement setup ###
* If you build with `MEASUREMENT=0`, application will not stop until terminated by user.
* In `src/rtod.h`, you can modify measurement setup.
```
/* Measurement */
#define MEASUREMENT_PATH      // Directory of measurement file
#define MEASUREMENT_FILE      // Measurement file name
#define OBJ_DET_CYCLE_IDX     // Count of measurement
```

### Usage ###

#### Original Darknet
```
$ ./darknet detector demo cfg/coco.data cfg weights 
      cfg: YOLO network configure file
  weights: weights file
```
#### +On-demand Capture
* See [How to set On-demand capture](#how-to-set-on--demand-capture).
```
$ ./darknet detector demo cfg/coco.data cfg weights 
      cfg: YOLO network configure file
  weights: weights file
```
#### Zero-Slack Pipeline
* **Zero-Slack Pipeline** needs **On-demand Capture**. See [How to set On-demand capture](#how-to-set-on--demand-capture).
* Build with `ZERO_SLACK=1`.
```
$ ./darknet detector rtod cfg/coco.data cfg weights
       cfg: YOLO network configure file
   weights: weights file
```
#### Contention-Free Pipeline
* **Contention-Free Pipeline** needs **On-demand Capture**. See [How to set On-demand capture](#how-to-set-on--demand-capture).
* Build with `CONTENTION_FREE=1`.
```
$ ./darknet detector rtod cfg/coco.data cfg weights
       cfg: YOLO network configure file
   weights: weights file
```
