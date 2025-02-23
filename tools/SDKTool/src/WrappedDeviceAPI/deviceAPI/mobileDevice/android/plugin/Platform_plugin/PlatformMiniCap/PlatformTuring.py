from devicePlatform.IPlatformProxy import *
from PlatformMinicap.minitouch.Minitouch import MinitouchAction
from PlatformMinicap.screen.ScreenMinicap import ScreenMinicap
from PlatformMinicap.adbkit.ADBClient import ADBClient
import cv2
from APIDefine import *


class PlatformTuring(IPlatformProxy):
    def __init__(self):
        IPlatformProxy.__init__(self)
        self.__deviceInfo = DeviceInfo()
        self.__device = None
        self.__config_ori = UI_SCREEN_ORI_LANDSCAPE
        self.__short_side = None
        self.__long_side = None
        self.__deviceHeightScale = None
        self.__deviceWidthScale = None
        self.__convertRate = 1.
        self.__height = -1
        self.__width = -1
        self.__orientation = 0

    def init(self, serial=None, is_portrait=True, long_edge=720, **kwargs):
        if serial is None:
            self.__logger = logging.getLogger("MinicapPlatform")
        else:
            self.__logger = logging.getLogger(serial)
        adb = ADBClient()
        self.__device = adb.device(serial)
        self.__short_side, self.__long_side = self.__device.wmsize()

        self.__CoordinateScale = float(self.__long_side) / long_edge

        if is_portrait:
            height = long_edge
            width = long_edge - 1
            self.__config_ori = UI_SCREEN_ORI_PORTRAIT
        else:
            width = long_edge
            height = width - 1
            self.__config_ori = UI_SCREEN_ORI_LANDSCAPE

        minicapPort = self._GetValuesInkwargs('minicapPort', 1111, kwargs)
        showscreen = self._GetValuesInkwargs('showRawScreen', False, kwargs)
        self.__minicap = ScreenMinicap(self.__device, showscreen, serial)
        if not self.__minicap.Initialize(height, width, minicapPort=minicapPort):
            self.__logger.error('init minicap failed')
            return False, "init minicap failed"
        
        height, width = self.__minicap.GetScreenSize()
        self.__height, self.__width = height, width

        minitouchPort = self._GetValuesInkwargs('minitouchPort', 1313, kwargs)
        self.__minitouch = MinitouchAction()
        if not self.__minitouch.init(serial, self.__device, width, height, minitouchPort=minitouchPort):
            self.__logger.error('init minitouch failed')
            return False, "init minitouch failed"
        
        self.__deviceInfo.display_width, self.__deviceInfo.display_height = self.__minicap.GetVMSize()
        self.__deviceInfo.touch_width, self.__deviceInfo.touch_height = self.__minitouch.GetVMSize()
        self.__deviceInfo.touch_slot_number = self.__minitouch.GetMaxContacts()

        self.__scale = long_edge * 1.0 / self.__deviceInfo.display_height

        if is_portrait:
            self.__game_width = self.__deviceInfo.display_width * self.__scale
            self.__game_height = long_edge
        else:
            self.__game_width = long_edge
            self.__game_height = self.__deviceInfo.display_width * self.__scale
        self.__regular_height = long_edge
        
        return True, str()
    
    def deinit(self):
        self.__minicap.closeMinicapStream()
        self.__minitouch.closeMinitouch()
        return True
    
    def get_image(self):
        image = self.__minicap.GetScreen()
        self.__orientation = self.__minicap.GetRotation()
        self.__minitouch.ChangeRotation(self.__orientation)
        
        # if image is None:
        #     return PP_RET_ERR, None
        # else:
        #     # return image as real orientation
        #     if self.__orientation  == 0:
        #         return 0, image
        #     elif self.__orientation == 1:
        #         return 0, cv2.flip(cv2.transpose(image), 0)
        #     elif self.__orientation == 2:
        #         # FIXME
        #         return 0, cv2.flip(cv2.transpose(image), 0)
        #     elif self.__orientation == 3:
        #         return 0, cv2.flip(cv2.transpose(image), 1)
        return PP_RET_OK, image

    def touch_up(self, contact=0):
        self.__minitouch.touch_up(contact=contact)
        self.__logger.info('touch up, contact:{}'.format(contact))

    def touch_down(self, px, py, contact=0, pressure=50):
        # _x, _y = self.__trans_xy(px, py)
        # if self.__orientation == 0:
        #     self.__minitouch.touch_down(px=px, py=py, contact=contact, pressure=pressure)
        # else:
        #     self.__minitouch.touch_down(px=py, py=px, contact=contact, pressure=pressure)
        self.__minitouch.touch_down(px=px, py=py, contact=contact, pressure=pressure)
        self.__logger.info('touch down, x:{}, y:{}, contact:{}'.format(px, py, contact))

    def touch_move(self, px, py, contact=0, pressure=50):
        self.__minitouch.touch_move(px=px, py=py, contact=contact, pressure=pressure)
        self.__logger.info('touch move, x:{}, y:{}, contact:{}'.format(px, py, contact))

    def touch_wait(self, milliseconds):
        self.__minitouch.touch_wait(milliseconds)
        self.__logger.info('touch wait:{0}'.format(milliseconds))
        
    def touch_reset(self):
        self.__minitouch.touch_reset()
        self.__logger.info('touch reset')

    def touch_finish(self):
        self.__minitouch.touch_finish()
        self.__logger.info('touch finish')

    def get_device_info(self):
        return self.__deviceInfo, str()

    def get_rotation(self):
        pass

    def install_app(self, apk_path):
        return self.__device.install(apk_path)

    def launch_app(self, package_name, activity_name):
        self.__logger.debug('Launch Game:[{0}/{1}]'.format(package_name, activity_name))
        self.wake()
        # self.__device.clear_app_data(package_name=MOBILEQQ_PACKAGE_NAME)
        self.exit_app(package_name)
        self.__device.launch_app(package_name=package_name, activity_name=activity_name)

    def exit_app(self, package_name):
        self.__device.kill_app(package_name=package_name)

    def current_app(self):
        return self.__device.current_app()

    def clear_app_data(self, app_package_name):
        self.__device.clear_app_data(package_name=app_package_name)

    def key(self, key):
        self.__device.keyevent(key)

    def text(self, text):
        self.__device.input_text(text)

    def sleep(self):
        self.__device.close_screen()

    def wake(self):
        self.__device.wake()
        self.__device.unlock()
        self.__device.keyevent('HOME')

    def vm_size(self):
        return self.__short_side, self.__long_side

    def take_screen_shot(self, target_path):
        self.__device.screenshot()
        self.__device.pull('/data/local/tmp/screenshot.png', target_path)

    def get_screen_ori(self):
        rot = self.__minicap.GetRotation()
        if rot in [0, 2]:
            return UI_SCREEN_ORI_PORTRAIT
        else:
            return UI_SCREEN_ORI_LANDSCAPE

    def adb_click(self, px, py):
        if self.get_screen_ori() != self.__config_ori:
            px, py = self._ConvertPos(px, py)
        px, py = self._ConvertCoordinates(px, py)
        self.__device.click(px, py)
    def adb_swipe(self, sx, sy, ex, ey, duration_ms=50):
        if self.get_screen_ori() != self.__config_ori:
            sx, sy = self._ConvertPos(sx, sy)
            ex, ey = self._ConvertPos(ex, ey)
        sx, sy = self._ConvertCoordinates(sx, sy)
        ex, ey = self._ConvertCoordinates(ex, ey)
        self.__device.swipe(sx, sy, ex, ey, durationMS=duration_ms)

    def device_param(self, packageName):
        deviceParam = dict()
        deviceParam['cpu'], deviceParam['mem'] = self.__device.cpu_mem_usage_with_top(packageName)
        deviceParam['temperature'] = self.__device.temperature()
        deviceParam['battery'] = self.__device.battery()
        if deviceParam['cpu'] == -1:
            self.__logger.error('get cpu param failed')
            return False

        if deviceParam['mem'] == -1:
            self.__logger.error('get mem param failed')
            return False

        if deviceParam['temperature'] == -1:
            self.__logger.error('get temperature param failed')
            return False

        if deviceParam['battery'] == -1:
            self.__logger.error('get battery param failed')
            return False
        return deviceParam

    def _ConvertPos(self, px, py):
        if self.__config_ori == UI_SCREEN_ORI_PORTRAIT:
            newPx = py
            newPy = self.__width - px
        else:
            newPx = self.__height - py
            newPy = px
        return newPx, newPy

    def _ConvertCoordinates(self, px, py):
        nx = px * self.__CoordinateScale
        ny = py * self.__CoordinateScale
        return int(nx), int(ny)

    def _GetValuesInkwargs(self, key, defaultValue, kwargs):
        if key not in kwargs:
            return defaultValue
        else:
            return kwargs[key]
    
    def __trans_xy2(self, x, y):
        if self.__game_width > self.__game_height:
            nx, ny = self.__game_height - y, x
            return int(self.__game_height), int(self.__game_width), int(nx), int(ny)
        else:
            nx, ny = x, y
            return int(self.__game_width), int(self.__game_height), int(nx), int(ny)

    def __trans_xy(self, x, y):
        orientation = self.__orientation

        if orientation == 0:
            nx, ny = x, y
        elif orientation == 1:   # counter-clockwise 90
            # nx, ny = self.__game_height - y, x
            nx, ny = self.__game_width - y, x
        elif orientation == 2:
            nx, ny = self.__game_width - y, x
        elif orientation == 3:  # clockwise 90
            nx, ny = y, self.__game_width - x
        else:
            nx, ny = x, y

        # _x, _y = int(nx / self.__scale), int(ny / self.__scale)
        # print("game size", self.__game_height, self.__game_width)
        if self.__game_width > self.__game_height:
            _touch_scale_x = nx / self.__game_height
            _touch_scale_y = ny / self.__game_width
        else:
            _touch_scale_x = nx / self.__game_width
            _touch_scale_y = ny / self.__game_height
        _x = int(self.__deviceInfo.touch_width * _touch_scale_x)
        _y = int(self.__deviceInfo.touch_height * _touch_scale_y)
        return _x, _y
