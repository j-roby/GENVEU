# GENVEU
gnveu is used to tunnel multiple network connections over a single UDP connection. It supports tunnelling of
Ethernet (layer 2) packets inside the Geneve protocol.
Each network tunnel is connected to the kernel via the tap(4) device driver. Packets read from the kernel
are encapsulated in Geneve before being sent on the UDP socket. Geneve packets received from the socket
are decapsulated and written to the tap(4) device driver. The Geneve protocol will also be used to filter
Ethernet traffic.


usage: gnveu [-46d] [-l address] [-p port] -t 120
-e /dev/tapX@vni
server [port]
and terminate with a non-zero exit code. Note VNI stands for Virtual Network Identifier.
gnveu takes the following command line arguments:

  -4 Force gnveu to use IPv4 addresses only.
  
  
  -6 Force gnveu to use IPv6 addresses only.
  
  
  -d Do not daemonise. If this option is specified, gnveu will run in the foreground and log to stderr. By
      default gnveu will daemonise.
     
     
  -t idle_timeout (value specified in seconds) Close and exit after idle_timeout duration is exceeded. If
      no traffic is received for a duration that exceeds the timeout, then the system is considered to be idle.
      The duration must be a postive value specified in seconds. If no idle_timeout is given (or is invalid),
      then an error message must be displayed. If a negative value is given, then no idle_timeout should
      occur.
      
      
   -l address (Optional and does not need to be used) Bind to the specified local address. By default
      gnveu does not bind to a local address.
      
      
   -p port (Optional and does not need to be used) Use the specified source port. By default gnveu
      should use the destination port as the source port.
      
      
    -e /dev/tapX@vni Tunnel enter/exit point for Ethernet (level 2) traffic for the specified tunnel device. The
      @vni must be specified. Note VNI stands for Virtual Network Identifier per the draft-ietf-nvo3-geneve-16
      specification. The VNI is used to filter the Ethernet traffic (depending on the payload type). When
      either of the following VNI is specified, only ethernet packets containing the corresponding payload
      type must be encapsulated and tunnelled. Other types of ethernet traffic should be filtered out and not
      tunnelled. Hint: Version field in IPv4 and IPv6 packets.
      
      
      VNI Traffic
        4096 IPv4 only (filter out other types)
        8192 IPv6 only (filter out other types)
        Any not listed above Any Ethernet



   server The address or host name of the remote tunnel endpoint.



   port Use the specified port on the server as the remote tunnel endpoint. By default gnveu should use port
      6081 as per the draft-ietf-nvo3-geneve-16 specification.
At least one tunnel must be configured.

If gnveu is unable to bind to the specified local address and port, connect to the remote server and port, or
open the specified tunnel interfaces, it should generate an appropriate error and terminate with a non-zero
exit status.

