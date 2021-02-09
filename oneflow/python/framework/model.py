"""
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""
from __future__ import absolute_import
from abc import ABC
from typing import Optional

from oneflow.python.framework.function_util import api_oneflow_function
from oneflow.python.oneflow_export import oneflow_export
from oneflow.python.framework.module import Module
from oneflow.python.framework.check_point import CheckPoint
from oneflow.python.ops.optimizer import Optimizer
from oneflow.python.ops.dataloader import DataLoader


@oneflow_export("nn.Model")
class Model(
    ABC,
    Module,
):
    r"""A high level API for model training and validation.
    """
    def __init__(self, is_function_style=False, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._context = ...
        self.is_function_style = is_function_style
        print("is_functoin_style:", self.is_function_style)

    def forward(self, *args, **kwargs):
        r"""Same as `nn.Module.forward()`, here is to define the operations you want to use for prediction.
        """
        return super().forward(*args, **kwargs)
    
    def training_step(self, *args, **kwargs):
        r"""Operates on a single batch of data from the training set and return loss.
        """
        raise NotImplementedError()

    def validation_step(self, *args, **kwargs):
        r"""Operates on a single batch of data from the validation set.
        """ 
    
    def configure_optimizers(self):
        r"""Choose what optimizers and learning-rate schedulers to use in your optimization.
        Normally you'd need one. But in the case of GANs or similar you might have multiple.
        """
        raise NotImplementedError()
    
    def optimizer_step(
        self,
        epoch: int = None,
        batch_idx: int = None,
        optimizer: Optimizer = None,
        optimizer_idx: int = None
    ) -> None:
        r"""Customized optimizer action.
        """
        # TODO(strint): consider lazy
        optimizer.step()
        optimizer.zero_grad()
    
    def print(self, *args, **kwargs) -> None:
        r"""Only print from root process.
        """
        if self.context.rank == 0:
            print(*args, **kwargs)
    
    def data_loader(self):
        raise NotImplementedError

    def eval_data_loader(self):
        raise NotImplementedError

    def _lazy_train_job(self, job_type, job_func_conf, batch_idx):
        @api_oneflow_function(type=job_type, function_config=job_func_conf)
        def job():
            batch = self.data_loader()
            loss = self.training_step(batch, batch_idx)
            self.optimizers[0].minimize(loss)
            return loss
        return job

    def _lazy_eval_job(self, job_func_conf, batch_idx):
        @api_oneflow_function(function_config=job_func_conf)
        def eval_job():
            batch = self.eval_data_loader()
            return self.validation_step(batch, batch_idx)
        return eval_job

    def fit(
        self,
        max_epochs: int = 1000,
        train_dataloader: Optional[DataLoader] = None,
        val_dataloaders: Optional[DataLoader] = None,
        default_checkpoint_dir: Optional[str] = None,
        training_config = None, # ToDo(strint): rm this here 
        eval_config = None, # ToDo(strint): rm this here 
    ):
        # TODO(strint): check config functions
        # 必须有training_step()/configure_optimizers()/train_dataloader
    
        self._has_optimizer_step = False
        self._has_train_data = False
        self._has_val_data = False
        
        if default_checkpoint_dir is None:
            self.checkpoint_dir = './'
        else:
            self.checkpoint_dir = default_checkpoint_dir
        
        # prepare optimizer
        optim_conf = self.configure_optimizers()
        if isinstance(optim_conf, Optimizer):
            self.optimizers = [optim_conf]
        elif isinstance(optim_conf, (list, tuple)):
            self.optimizers = optim_conf
    
        self.max_epochs = max_epochs
    
        # is eager or lazy
        # self._context.is_eager = False 
    
        train_job = self._lazy_train_job("train", training_config, 0)
        eval_job = self._lazy_eval_job(eval_config, 0)
        check_point = CheckPoint()
        if not self.args.model_load_dir:
            check_point.init()
        else:
            check_point.load(self.args.model_load_dir)

        for epoch in range(0, self.max_epochs):
            self.current_epoch = epoch
            print("fit epoch : ", epoch)
            loss = train_job().get().mean()
            fmt_str = "{:>12}  {:>12}  {:>12.6f}"
            print(fmt_str.format(epoch, "train loss:", loss))
            if (epoch + 1) % 10 == 0:
                eval_loss = eval_job().get().mean()
                print(
                    fmt_str.format(
                        epoch, "eval loss:", eval_loss
                    )
                )
        self.save_checkpoint(self.checkpoint_dir)
    
    def save_checkpoint(
        self,
        filepath,
    ):
        r"""Save model states as a checkpoint.
        """
        pass


    def load_checkpoint(
        self,
        filepath,
    ):
        r"""Load model states from a checkpoint.
        """
        pass