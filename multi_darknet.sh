./darknet_1 detector demo cfg/coco.data cfg/yolov3.cfg /aveesSSD/darknet/yolov3.weights -c 0 -buffer_size 0 &
./darknet_2 detector demo cfg/coco.data cfg/yolov3.cfg /aveesSSD/darknet/yolov3.weights -c 1 -buffer_size 0
#sleep 30
#echo "Killing test darknet..."
#killall -s SIGKILL 'darknet_0' & 
#killall -s SIGKILL 'darknet_1'  
