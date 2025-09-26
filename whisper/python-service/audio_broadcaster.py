import struct
import time

import numpy as np
import zmq
from omegaconf import OmegaConf
from scipy.io import wavfile
from scipy.signal import resample_poly


def zmq_tcp_publisher(socket, port, ip_address, message):

    print(f"Sending audio TCP on port {port}, group {ip_address}...")

    socket.send(message)


if __name__ == "__main__":
    cfg = OmegaConf.load("defaults.yaml")
    if not "ip_address" in cfg:
        cfg.ip_address = "localhost"

    context = zmq.Context()
    socket = context.socket(zmq.DEALER)
    socket.setsockopt(zmq.IDENTITY, b"ab") # Set the socket identity
    socket.setsockopt(zmq.SNDHWM, 1) # Set the high-water mark for outbound messages
    socket.connect(f"tcp://{cfg.ip_address}:{cfg.port}")
    request_id = bytes([1, 0, 0, 0, 0, 0, 0, 0])
    fs, audio = wavfile.read("JugWine-0-100.wav")
    audio = np.array(audio / (0.5 * 2**16), dtype="float32")
    print(f"audio pre resample; {audio.shape}")
    audio = resample_poly(audio, 32000, fs)
    print(f"audio post resample; {audio.shape}")
    idx = 0

    while True:
        if idx >= len(audio):
            idx = 0

        chunk = audio[idx : (idx + (32000 * 5)), 0]
        print(f"chunk; {chunk.shape}")
        chunk_bytes = chunk.tobytes()
        message = request_id + chunk_bytes
        print(f"message len; {len(message)}")
        zmq_tcp_publisher(socket, cfg.port, cfg.ip_address, message)
        print(f"broadcasting on {cfg.ip_address}:{cfg.port}")
        time.sleep(1)
        print("next chunk...")
        result = None
        while result == None:
            result = socket.recv_json()
            print(result)
