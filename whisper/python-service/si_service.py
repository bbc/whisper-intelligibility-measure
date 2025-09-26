import argparse
import asyncio
import json
import multiprocessing as mp
from os.path import join
import uuid
import numpy as np
import zmq
import zmq.asyncio
from inference import model_init, si_inference
from omegaconf import OmegaConf
import platform

# globals
requests_outstanding = 0
requests_queue_limit = 100

class SI_Pool:
    def __init__(self, cfg):
        path = join(cfg.model_path, cfg.regressor)
        if "pool_size" in cfg:
            self.process_pool = mp.Pool(initializer=model_init, processes=cfg.pool_size, initargs=(path,))
        else:
            self.process_pool = mp.Pool(initializer=model_init, initargs=(path,))

    def pool_init(self):
        pass

    async def get_inference(self, audio_list):
        # Use asyncio.to_thread to offload the blocking call
        results = await asyncio.to_thread(self.process_pool.map, si_inference, audio_list)
        return {"result": list(results)}


def parse_args(defaults):
    parser = argparse.ArgumentParser(description="Whisper Intelligibility Measure service")
    parser.add_argument(
        "-m", "--model_path", type=str, help="Model file path", required=False
    )
    parser.add_argument("-p", "--port", help="port", required=False)
    args = parser.parse_args()
    for a, v in vars(args).items():
        if not v == None:
            defaults[a] = v
    return defaults


async def handle_message(envelope, message):
    # A request rejection (e.g, queue too big) should be handled as follows;
    # The presence of a "error" key is enough for a client to assume failure of some sort.

    # result = {
        # "request_id": request_id,
        # "error": "overloaded",
        # }
    # result_json = json.dumps(result)
    # print(f"Sending rejection to {envelope} for ID {request_id}")
    # await socket.send_multipart([envelope, result_json.encode('utf-8')])

    result = {}
    audio = None
    
    try:
        # Extract request id
        result["request_id"] = int.from_bytes(message[:8], byteorder="little", signed=False)
    except:
        result["error"] = "unable to parse request - request ID"
    
    if "error" not in result:
        try:
            # Extract audio data
            audio_data = message[8:]
            audio = np.frombuffer(audio_data, dtype=np.float32) # Create a NumPy array of floats
        except:
            result["error"] = "unable to parse request - audio data"
    
    global requests_outstanding
    global requests_queue_limit
    
    if "error" not in result:
        print(f'Received {len(audio)} samples from {envelope} with ID {result["request_id"]}')
        if requests_outstanding >= requests_queue_limit:
            result["error"] = "queue full"
            
    if "error" not in result:
        requests_outstanding = requests_outstanding + 1
        try:
            # Analyse
            si_result = await si_pool.get_inference([audio])
            result["result"] = si_result["result"]
        except:
            result["error"] = "analysis failed"
        requests_outstanding = requests_outstanding - 1
    
    result_json = json.dumps(result)
    if "error" in result:
        print(f'Sending rejection to {envelope} for ID {result["request_id"]} - {result["error"]}')
    else:
        print(f'Sending result to {envelope} for ID {result["request_id"]}: {result["result"][0]}')
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
    if "max_queue" in cfg:
        requests_queue_limit = cfg.max_queue

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
