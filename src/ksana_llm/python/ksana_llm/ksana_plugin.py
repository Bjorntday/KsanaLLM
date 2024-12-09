# Copyright 2024 Tencent Inc.  All rights reserved.
#
# ==============================================================================

import importlib.util
import os
import sys
from concurrent import futures
from dataclasses import dataclass
from typing import List, Optional

import libtorch_serving


@dataclass
class PluginConfig:
    model_dir: str = ""  # the directory of the serving model
    config_file: str = ""  # the path of the yaml config file
    plugin_name: str = ""  # the name of this plugin
    # Whether to enable TensorRT inference for plugin model (e.g., ViT).
    # The default value is true. But if trt inference is abnormal (NaN),
    # you need to manually configure it in yaml to adjust to False to use
    # the Torch inference.
    enable_trt: bool = True


class KsanaPlugin(object):
    """
    This class is designed to dynamically load and manage plugins for the Ksana framework.
    It allows for the initialization and post-processing of plugins specified by a file path.
    """

    def __init__(self, config: PluginConfig):
        """
        Initializes the KsanaPlugin instance by loading a plugin from the given path.

        :param plugin_config: The specified plugin configuration.
        """
        plugin_path = KsanaPlugin.search_plugin_path(config.model_dir,
                                                     config.plugin_name)
        self._ksana_plugin = self.load_plugin(plugin_path)
        if hasattr(self._ksana_plugin, 'init_plugin'):
            kwargs = {
                "preprocess": True,
                "postprocess": True,
                "model_path": config.model_dir,
                "config_file": config.config_file,
                "enable_trt": config.enable_trt,
            }
            self._ksana_plugin.init_plugin(**kwargs)
        if self._ksana_plugin is None:
            return

        # We utilize a thread pool to manage the execution of plugin operations, to control
        # the level of concurrency. This is primarily aimed at preventing concurrent model
        # inference task from competing for GPU memory and computational resources.
        try:
            max_workers = int(os.getenv('KSANA_PLUGIN_MAX_WORKERS', 1))
        except ValueError:
            max_workers = 1

        self._thread_pool = futures.ThreadPoolExecutor(max_workers)

    def __del__(self):
        """
        Clean up the thread pool when the instance is destroyed.
        """
        if hasattr(self, "_thread_pool"):
            self._thread_pool.shutdown(wait=True)

    def load_plugin(self, plugin_path: Optional[str]):
        """
        Dynamically loads the plugin module located at the specified path.

        :param plugin_path: The file system path to the plugin module to be loaded.
        :return: An instance of the loaded plugin class if successful, None otherwise.
        """
        # Plugin is not provided
        if plugin_path is None:
            return None
        try:
            spec = importlib.util.spec_from_file_location("ksana_plugin", plugin_path)
            if spec is None:
                print(f"[W] Load plugin failed: {plugin_path} can not be imported.")
                return None
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            class_name = "KsanaPlugin"
            if hasattr(module, class_name):
                ksana_plugin = getattr(module, class_name)
                print("[I] Plugin is loaded.")
                return ksana_plugin()
            else:
                print("[W] Load plugin failed: the plugin does not have KsanaPlugin class.")
                return None
        except Exception as e:  # pylint: disable=broad-except
            print(f"[W] Load plugin failed: {e}")
            return None

    def preprocess(self,
                   ksana_python_input: libtorch_serving.KsanaPythonInput,
                   **kwargs):
        """
        Invokes the preprocess method of the loaded plugin, if available.
        """
        if self._ksana_plugin is None:
            return

        kwargs.update({
            "ksana_python_input": ksana_python_input,
        })
        future = self._thread_pool.submit(self._ksana_plugin.preprocess, **kwargs)
        return future.result()

    def postprocess(self,
                    ksana_python_input: libtorch_serving.KsanaPythonInput,
                    ksana_python_output: libtorch_serving.KsanaPythonOutput,
                    **kwargs):
        """
        Invokes the postprocess method of the loaded plugin, if available.
        """
        if self._ksana_plugin is None:
            return

        kwargs.update({
            "ksana_python_input": ksana_python_input,
            "ksana_python_output": ksana_python_output,
        })
        future = self._thread_pool.submit(self._ksana_plugin.postprocess, **kwargs)
        return future.result()

    @staticmethod
    def search_plugin_path(model_dir: str, plugin_name: str) -> Optional[str]:
        """
        Search the path where the ksana plugin is located.
        """
        sys_path: List[str] = sys.path
        # 1. Search within the model dir
        target_file = os.path.join(model_dir, "ksana_plugin.py")
        if os.path.isfile(target_file):
            return target_file

        # 2. Search within the current working directory
        python_dir = sys_path[0]
        target_file = os.path.join(python_dir, "ksana_plugin", plugin_name, "ksana_plugin.py")
        if os.path.isfile(target_file):
            return target_file

        # 3. Search within the ksana_llm package
        package_suffix = "site-packages"
        for python_dir in sys_path[1:]:
            if python_dir.endswith(package_suffix):
                target_file = os.path.join(python_dir, "ksana_llm", "ksana_plugin",
                                           plugin_name, "ksana_plugin.py")
                if os.path.isfile(target_file):
                    return target_file

        return None
