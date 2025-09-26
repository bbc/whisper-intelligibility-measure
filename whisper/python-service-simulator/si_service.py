import argparse
import json
import asyncio
import multiprocessing as mp
import uuid
import numpy as np
import zmq
import zmq.asyncio
from inference import model_init, si_inference
from omegaconf import OmegaConf
import platform

class SI_Pool:
    def __init__(self, cfg):
        if "pool_size" in cfg:
            self.process_pool = mp.Pool(initializer=model_init, processes=cfg.pool_size, initargs=("fake_path",))
        else:
            self.process_pool = mp.Pool(initializer=model_init, initargs=("fake_path",))

    def pool_init(self):
        pass

    async def get_inference(self, audio_list):
        # TODO: Currently temp random delay to simulate work
        await asyncio.sleep(np.random.uniform(3, 8))
        # Use asyncio.to_thread to offload the blocking call
        results = await asyncio.to_thread(self.process_pool.map, si_inference, audio_list)
        return {"result": list(results)}


def parse_args(defaults):
    parser = argparse.ArgumentParser(description="Whisper Service Simulator")
    parser.add_argument("-p", "--port", help="port", required=False)
    args = parser.parse_args()
    for a, v in vars(args).items():
        if not v == None:
            defaults[a] = v
    return defaults


async def handle_message(envelope, message):
    request_id = int.from_bytes(message[:8], byteorder="little", signed=False)
    
    # Simulate req rejection (e.g, job queue too long)
    if np.random.random() < 0.2:
        result = {
            "request_id": request_id,
            "error": "overloaded",
            }
        result_json = json.dumps(result)
        print(f"Sending rejection to {envelope} for ID {request_id}")
        await socket.send_multipart([envelope, result_json.encode('utf-8')])
    
    else:
        audio_data = message[8:]
        audio = np.frombuffer(audio_data, dtype=np.float32) # Create a NumPy array of floats
        print(f"Received {len(audio)} samples from {envelope} with ID {request_id}")
        result = await si_pool.get_inference([audio])
        result["request_id"] = request_id
        result_json = json.dumps(result)
        print(f"Sending result to {envelope} for ID {request_id}")
        await socket.send_multipart([envelope, result_json.encode('utf-8')])


# Main async loop to receive and handle multiple messages concurrently
async def main():
    while True:
        envelope, data = await socket.recv_multipart()  # Non-blocking receive
        asyncio.create_task(handle_message(envelope, data))  # Process each message concurrently

# Entry point to run the server
if __name__ == "__main__":
    cfg = OmegaConf.load("defaults.yaml")
    cfg = parse_args(cfg)

    si_pool = SI_Pool(cfg)
    
    # Fix for Windows event loop issue with ZeroMQ
    if platform.system() == "Windows":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    
    # Generate a unique ID for this server
    server_uuid = str(uuid.uuid4())
    server_identity = "server" + server_uuid

    # Port and socket setup
    context = zmq.asyncio.Context()
    socket = context.socket(zmq.ROUTER)
    socket.setsockopt_string(zmq.IDENTITY, server_identity)
    socket.bind(f"tcp://*:{cfg.port}")
    
    print(f"{server_identity}: connected!")
    
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Server shutting down...")
