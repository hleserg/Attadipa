"""Remote control of a watch, or of the simulator standing in for one.

`protocol` is the wire format and knows nothing about a socket. `client` adds
the transport and the conversation. `scenario` runs a list of steps. The command
line tool is `tools/watch_control.py`.
"""
