"""Thin obs-websocket 5 wrapper (obsws-python) with the handful of calls the tests need.

Requests are sent with the raw camelCase payloads from the obs-websocket protocol so the
field names match the protocol document 1:1.
"""
from __future__ import annotations

import base64
import io
import time
from typing import Any, Callable, Dict, List, Optional

import numpy as np

from harness import paths, pattern


def abgr(rgb, alpha: int = 255) -> int:
    """OBS colour ints are 0xAABBGGRR."""
    r, g, b = (int(c) for c in rgb[:3])
    return (int(alpha) << 24) | (b << 16) | (g << 8) | r


class ObsClient:
    def __init__(self, host: str = paths.WS_HOST, port: int = paths.WS_PORT, password: str = paths.WS_PASSWORD, timeout: float = 15.0):
        import obsws_python as obs

        self.req = obs.ReqClient(host=host, port=port, password=password, timeout=timeout)
        self.scene = paths.SCENE_NAME

    def close(self) -> None:
        try:
            self.req.base_client.ws.close()
        except Exception:
            pass

    # -- raw ---------------------------------------------------------------------------------
    def call(self, request_type: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        return self.req.send(request_type, data, raw=True) or {}

    def vendor(self, vendor: str, request_type: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        return self.call("CallVendorRequest", {"vendorName": vendor, "requestType": request_type, "requestData": data or {}})

    # -- general ------------------------------------------------------------------------------
    def version(self) -> Dict[str, Any]:
        return self.call("GetVersion")

    def stats(self) -> Dict[str, Any]:
        return self.call("GetStats")

    def video_settings(self) -> Dict[str, Any]:
        return self.call("GetVideoSettings")

    def set_video_settings(self, **kw) -> None:
        self.call("SetVideoSettings", kw)

    # -- profiles / scenes --------------------------------------------------------------------
    def profiles(self) -> Dict[str, Any]:
        return self.call("GetProfileList")

    def current_profile(self) -> str:
        return self.profiles()["currentProfileName"]

    def set_profile(self, name: str) -> None:
        self.call("SetCurrentProfile", {"profileName": name})

    def current_scene(self) -> str:
        return self.call("GetCurrentProgramScene")["currentProgramSceneName"]

    def scenes(self) -> List[str]:
        return [s["sceneName"] for s in self.call("GetSceneList")["scenes"]]

    def set_current_scene(self, name: str) -> None:
        self.call("SetCurrentProgramScene", {"sceneName": name})

    def create_scene(self, name: str) -> None:
        self.call("CreateScene", {"sceneName": name})

    def remove_scene(self, name: str) -> None:
        self.call("RemoveScene", {"sceneName": name})

    # -- inputs -------------------------------------------------------------------------------
    def inputs(self, kind: Optional[str] = None) -> List[Dict[str, Any]]:
        data = {"inputKind": kind} if kind else {}
        return self.call("GetInputList", data)["inputs"]

    def input_names(self) -> List[str]:
        return [i["inputName"] for i in self.inputs()]

    def create_input(self, name: str, kind: str, settings: Optional[Dict[str, Any]] = None, scene: Optional[str] = None, enabled: bool = True) -> int:
        resp = self.call(
            "CreateInput",
            {
                "sceneName": scene or self.scene,
                "inputName": name,
                "inputKind": kind,
                "inputSettings": settings or {},
                "sceneItemEnabled": enabled,
            },
        )
        return int(resp["sceneItemId"])

    def remove_input(self, name: str) -> None:
        self.call("RemoveInput", {"inputName": name})

    def input_settings(self, name: str) -> Dict[str, Any]:
        return self.call("GetInputSettings", {"inputName": name})["inputSettings"]

    def set_input_settings(self, name: str, settings: Dict[str, Any], overlay: bool = True) -> None:
        self.call("SetInputSettings", {"inputName": name, "inputSettings": settings, "overlay": overlay})

    def property_items(self, name: str, prop: str) -> List[Dict[str, Any]]:
        return self.call("GetInputPropertiesListPropertyItems", {"inputName": name, "propertyName": prop})["propertyItems"]

    def create_color_source(self, name: str, rgb, alpha: int = 255, width: Optional[int] = None, height: Optional[int] = None) -> int:
        settings: Dict[str, Any] = {"color": abgr(rgb, alpha)}
        if width:
            settings["width"] = int(width)
        if height:
            settings["height"] = int(height)
        return self.create_input(name, "color_source_v3", settings)

    def create_receiver(self, name: str, sender: Optional[str] = None, composite_mode: Optional[int] = None, tick_speed: int = 1) -> int:
        settings: Dict[str, Any] = {"spoutsenders": sender or "usefirstavailablesender", "tickspeedlimit": tick_speed}
        if composite_mode is not None:
            settings["compositemode"] = int(composite_mode)
        return self.create_input(name, paths.RECEIVER_KIND, settings)

    # -- scene items --------------------------------------------------------------------------
    def scene_items(self, scene: Optional[str] = None) -> List[Dict[str, Any]]:
        return self.call("GetSceneItemList", {"sceneName": scene or self.scene})["sceneItems"]

    def item_id(self, name: str, scene: Optional[str] = None) -> int:
        return int(self.call("GetSceneItemId", {"sceneName": scene or self.scene, "sourceName": name})["sceneItemId"])

    def transform(self, name: str, scene: Optional[str] = None) -> Dict[str, Any]:
        scene = scene or self.scene
        return self.call("GetSceneItemTransform", {"sceneName": scene, "sceneItemId": self.item_id(name, scene)})["sceneItemTransform"]

    def set_transform(self, name: str, scene: Optional[str] = None, **transform) -> None:
        scene = scene or self.scene
        self.call("SetSceneItemTransform", {"sceneName": scene, "sceneItemId": self.item_id(name, scene), "sceneItemTransform": transform})

    def set_item_index(self, name: str, index: int, scene: Optional[str] = None) -> None:
        scene = scene or self.scene
        self.call("SetSceneItemIndex", {"sceneName": scene, "sceneItemId": self.item_id(name, scene), "sceneItemIndex": int(index)})

    def set_item_enabled(self, name: str, enabled: bool, scene: Optional[str] = None) -> None:
        scene = scene or self.scene
        self.call("SetSceneItemEnabled", {"sceneName": scene, "sceneItemId": self.item_id(name, scene), "sceneItemEnabled": bool(enabled)})

    def source_size(self, name: str, scene: Optional[str] = None) -> tuple[int, int]:
        t = self.transform(name, scene)
        return int(round(t["sourceWidth"])), int(round(t["sourceHeight"]))

    # -- filters ------------------------------------------------------------------------------
    def add_filter(self, source: str, filter_name: str, kind: str, settings: Optional[Dict[str, Any]] = None) -> None:
        self.call("CreateSourceFilter", {"sourceName": source, "filterName": filter_name, "filterKind": kind, "filterSettings": settings or {}})

    def add_spout_filter(self, source: str, sender_name: str = paths.FILTER_SENDER, filter_name: str = "Spout Filter") -> str:
        self.add_filter(source, filter_name, paths.FILTER_KIND, {"spout_filter_name": sender_name})
        return filter_name

    def set_filter_enabled(self, source: str, filter_name: str, enabled: bool) -> None:
        self.call("SetSourceFilterEnabled", {"sourceName": source, "filterName": filter_name, "filterEnabled": bool(enabled)})

    def set_filter_settings(self, source: str, filter_name: str, settings: Dict[str, Any], overlay: bool = True) -> None:
        self.call("SetSourceFilterSettings", {"sourceName": source, "filterName": filter_name, "filterSettings": settings, "overlay": overlay})

    def remove_filter(self, source: str, filter_name: str) -> None:
        self.call("RemoveSourceFilter", {"sourceName": source, "filterName": filter_name})

    def filters(self, source: str) -> List[Dict[str, Any]]:
        return self.call("GetSourceFilterList", {"sourceName": source})["filters"]

    # -- Tools output -------------------------------------------------------------------------
    def output_set_sender(self, sender_name: str) -> None:
        self.call("SetOutputSettings", {"outputName": paths.OUTPUT_NAME, "outputSettings": {"senderName": sender_name}})

    def output_start(self) -> None:
        self.call("StartOutput", {"outputName": paths.OUTPUT_NAME})

    def output_stop(self) -> None:
        self.call("StopOutput", {"outputName": paths.OUTPUT_NAME})

    def output_status(self) -> Dict[str, Any]:
        return self.call("GetOutputStatus", {"outputName": paths.OUTPUT_NAME})

    def output_active(self) -> bool:
        return bool(self.output_status().get("outputActive"))

    def outputs(self) -> List[Dict[str, Any]]:
        return self.call("GetOutputList")["outputs"]

    # -- screenshots --------------------------------------------------------------------------
    def screenshot(self, source: Optional[str] = None, width: Optional[int] = None, height: Optional[int] = None) -> np.ndarray:
        """RGBA ndarray of a source (or the current program scene when ``source`` is None)."""
        from PIL import Image

        payload: Dict[str, Any] = {"sourceName": source or self.current_scene(), "imageFormat": "png", "imageCompressionQuality": 100}
        if width:
            payload["imageWidth"] = int(width)
        if height:
            payload["imageHeight"] = int(height)
        data = self.call("GetSourceScreenshot", payload)["imageData"]
        raw = base64.b64decode(data.split(",", 1)[1])
        return pattern.from_pil(Image.open(io.BytesIO(raw)))

    # -- housekeeping -------------------------------------------------------------------------
    def clear(self) -> None:
        """Remove every input/scene the tests created and stop the Tools output."""
        try:
            if self.output_active():
                self.output_stop()
        except Exception:
            pass
        for name in self.input_names():
            try:
                self.remove_input(name)
            except Exception:
                pass
        try:
            if self.current_scene() != self.scene and self.scene in self.scenes():
                self.set_current_scene(self.scene)
        except Exception:
            pass
        for scene in self.scenes():
            if scene != self.scene:
                try:
                    self.remove_scene(scene)
                except Exception:
                    pass

    def wait(self, predicate: Callable[[], bool], timeout: float = 5.0, interval: float = 0.1, what: str = "condition") -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if predicate():
                    return True
            except Exception:
                pass
            time.sleep(interval)
        return False

    def wait_source_size(self, name: str, size, timeout: float = 5.0) -> tuple[int, int]:
        want = (int(size[0]), int(size[1]))
        last = (0, 0)

        def check() -> bool:
            nonlocal last
            last = self.source_size(name)
            return last == want

        self.wait(check, timeout)
        return last
