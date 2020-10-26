import oneflow as flow
import oneflow.typing as tp

import argparse
import cv2
import grpc
import numpy as np
import prediction_service_pb2_grpc as grpc_service_pb2
import prediction_service_pb2 as predict_message_pb2
import resnet_model

from concurrent import futures
from imagenet1000_clsidx_to_labels import clsidx_2_labels

def get_parser():
    def float_list(x):
        return list(map(float, x.split(',')))

    parser = argparse.ArgumentParser("flags for grpc server demo")
    parser.add_argument("--server_address", type = str, default = "localhost", help = "")
    parser.add_argument("--server_port", type = int, default = 8000, help = "")
    parser.add_argument("--model_load_dir", type = str,
                        default = None, help = "model load directory if need")
    parser.add_argument('--rgb-mean', type = float_list, default = [123.68, 116.779, 103.939],
                        help = 'a tuple of size 3 for the mean rgb')
    parser.add_argument('--rgb-std', type = float_list, default = [58.393, 57.12, 57.375],
                        help = 'a tuple of size 3 for the std rgb')
    return parser

parser = get_parser()
args = parser.parse_args()

def preprocess_image(im):
    im = cv2.resize(im.astype('uint8'), (224, 224))
    im = cv2.cvtColor(im, cv2.COLOR_BGR2RGB)
    im = im.astype('float32')
    im = (im - args.rgb_mean) / args.rgb_std
    im = np.transpose(im, (2, 0, 1))
    im = np.expand_dims(im, axis=0)
    return np.ascontiguousarray(im, 'float32')

@flow.global_function("predict", flow.function_config())
def InferenceNet(images: tp.Numpy.Placeholder((1, 3, 224, 224), dtype=flow.float)) -> tp.Numpy:
    logits = resnet_model.resnet50(images, training=False)
    predictions = flow.nn.softmax(logits)
    return predictions

check_point = flow.train.CheckPoint()
print("start load resnet50 model.")
check_point.load(args.model_load_dir)
print("load resnet50 model done.")

class PredictionServiceServer(grpc_service_pb2.PredictionService):

    def __init__(self, *args, **kwargs):
        pass

    def Predict(self, predict_request, context):
        message_from_client = f'Server received data_len: "{len(predict_request.np_array_content)}", data_shape: "{predict_request.np_array_shapes}"'
        print(message_from_client)

        deserialized_bytes  = np.frombuffer(predict_request.np_array_content, dtype = np.uint8)
        image = np.reshape(deserialized_bytes, newshape = tuple(predict_request.np_array_shapes))

        predictions = InferenceNet(preprocess_image(image))
        clsidx = predictions.argmax()
        print("predicted class name: %s, prob: %f\n" % (clsidx_2_labels[clsidx], predictions.max()))

        result = {'predicted_class': clsidx_2_labels[clsidx], 'predicted_score': predictions.max()}

        return predict_message_pb2.PredictResponse(**result)

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    grpc_service_pb2.add_PredictionServiceServicer_to_server(PredictionServiceServer(), server)
    server.add_insecure_port('%s:%d' % (args.server_address, args.server_port))
    server.start()
    print("start gprc server")
    server.wait_for_termination()

serve()
