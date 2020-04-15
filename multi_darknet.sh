./darknet detector demo cfg/coco.data cfg/yolov3.cfg /aveesSSD/darknet/yolov3.weights -c 0 -buffer_size 0 -offset 0 -process_num 0 &
./darknet detector demo cfg/coco.data cfg/yolov3.cfg /aveesSSD/darknet/yolov3.weights -c 1 -buffer_size 0 -offset 0 -process_num 1
#sleep 30
#echo "Killing test darknet..."
#killall -s SIGKILL 'darknet_0' & 
#killall -s SIGKILL 'darknet_1'  
