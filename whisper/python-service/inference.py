import os

import numpy as np
import speechbrain as sb
import torch
from omegaconf import OmegaConf
from scipy.io import wavfile
from scipy.signal import resample_poly as rs
from transformers import WhisperFeatureExtractor

from models.ni_feat_extractors import WhisperFull_feats
from models.ni_predictor_models import MetricPredictorLSTM_layers

# globals
cfg = None
fe = WhisperFull_feats(
    pretrained_model=None,
    use_feat_extractor=True,
    layer=-1,
)
model = None
fs = 16000


def si_inference(x):
    """_summary_

    Args:
        x (arraylike): 5 second window of audio to be evaluated

    Returns:
        float: SI estimate
    """
    if fs > 16000:
        x = rs(x, 16000, fs)
    if fs < 16000:
        x = rs(x, fs, 16000)
    if len(x) < cfg.inference_length:
        zeros = np.zeros(cfg.inference_length - len(x))
        x = np.concatenate([x, zeros])
    if len(x) > cfg.inference_length:
        x = x[: cfg.inference_length]
    
    x = fe(torch.tensor(x, dtype=torch.float32).unsqueeze(0))
    a = torch.nn.utils.rnn.pack_padded_sequence(
        torch.Tensor(x),
        lengths=[len(x) * x.shape[1]],
        batch_first=True,
        enforce_sorted=False,
    )
    return float(model(a, True)[0].detach().numpy()[0][0])


def model_init(model_path=None, sample_rate=16000):
    """model initialisation

    Args:
        model_path (string, optional): path to regressor model. Defaults to None.
        sample_rate (int, optional): sample rate of client audio. Defaults to 16000.
    """
    global fs
    fs = sample_rate
    global cfg
    cfg = OmegaConf.load("defaults.yaml")
    current_directory = os.getcwd()
    
    if model_path == None:
        model_path = os.path.join(current_directory, cfg.model_path, cfg.regressor)

    print(f"Model from {model_path}. CWD: {current_directory}.")
        
    global model
    model = MetricPredictorLSTM_layers(
        dim_extractor=768,
        hidden_size=768 // 2,
        activation=torch.nn.LeakyReLU,
        att_pool_dim=768,
        num_layers=12,
    )

    try:
        print(f"Loading model from {model_path}. CWD: {current_directory}.")
        model.load_state_dict(
            torch.load(model_path, map_location=torch.device(cfg.device), weights_only=True)
        )
    except Exception as e:
        print(f"Init failed: for {model_path}")
        print(e)


if __name__ == "__main__":
    model_init()
