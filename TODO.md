OK - set stream format (select capability)
OK - copy media sample buffer -> implement read()
OK - flip image when copying
2018-03-30:
OK - check on access violation when executing get function
	 cause: thread 1 finished before thread 2 and destroyed cam object
	 solution: use events to interlock

first setup
============
main
createThread -> getCam (thread 1)
				- cam object in heap
				- CreateThread -------> startStopGraphViaCon (thread 2)
										- read console
										- display frame (cv::imshow)
observations:
- thread 1 needed to use cv::imshow in main
- thread 2 needed to display frame	

second setup
============				
main
CreateThread -> getCam (thread 1)
      |			- cam object in heap
	  |			- wait for "destroy cam" event
- cam::read(frame)	  
- display frame
- if exit -> signal "destroy cam" event

third setup (planned)
============
main
CreateThread -> initGraph (thread 1)
				- isInit ->
	  
