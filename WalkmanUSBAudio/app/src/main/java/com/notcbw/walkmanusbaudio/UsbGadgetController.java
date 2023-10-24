package com.notcbw.walkmanusbaudio;

import android.util.Log;

import com.topjohnwu.superuser.Shell;

import java.util.HashMap;
import java.util.List;

public class UsbGadgetController {
    private static final String TAG = "UsbGadgetController";

    private String gadgetId;
    private String configId;
    private String defaultUDC;

    private String funcUac2 = null;
    private String funcMs = null;
    private static final String CONF_UAC2 = "f8";
    private static final String CONF_MS = "f9";
    private boolean uac2Enabled = false;
    private boolean msEnabled = false;

    public UsbGadgetController() {
        Shell s = Shell.getShell();
        if (!(s.isAlive() && s.isRoot()))
            throw new RuntimeException("Failed to get root shell!");

        Shell.Result res;
        res = Shell.cmd("ls /config/usb_gadget").exec();
        if (res.isSuccess()) {
            gadgetId = res.getOut().get(0).trim();
        } else {
            throw new RuntimeException("No ConfigFS folder for USB gadget!");
        }

        res = Shell.cmd(String.format("ls /config/usb_gadget/%s/configs", gadgetId)).exec();
        if (res.isSuccess()) {
            configId = res.getOut().get(0).trim();
        } else {
            configId = "b.1";
            Shell.cmd(String.format("mkdir /config/usb_gadget/%s/configs/b.1", gadgetId));
        }

        res = Shell.cmd(String.format("cat /config/usb_gadget/%s/UDC", gadgetId)).exec();
        if (res.isSuccess()) {
            defaultUDC = res.getOut().get(0).trim();
            if (defaultUDC == "")
                defaultUDC = "ci_hdrc.0";
        } else {
            throw new RuntimeException("Cannot get default UDC device");
        }
    }

    private List<String> getAvailableFunctions() {
        Shell.Result res;
        res = Shell.cmd(String.format("ls /config/usb_gadget/%s/functions", gadgetId)).exec();
        if (res.isSuccess()) {
            return res.getOut();
        } else {
            return null;
        }
    }

    private boolean addFunction(String func) {
        Shell.Result res;
        res = Shell.cmd(String.format("mkdir /config/usb_gadget/%s/functions/%s", gadgetId, func)).exec();
        return res.isSuccess();
    }

    public void enableUAC2() {
        // check if uac2 is already in the function folder
        List<String> func = this.getAvailableFunctions();
        funcUac2 = null;
        if (func != null) {
            for (String s : func) {
                if (s.contains("uac2"))
                    funcUac2 = s.trim();
            }
        }

        // if not, mkdir
        if (funcUac2 == null) {
            funcUac2 = "uac2.0";
            if (!addFunction(funcUac2))
                throw new RuntimeException("UAC2 is not supported on this device!");
        }

        // symbolic link function to config to enable uac2 function
        Shell.Result res = Shell.cmd(String.format("ln -s /config/usb_gadget/%s/functions/%s " +
                            "/config/usb_gadget/%s/configs/%s/%s",
                    gadgetId, funcUac2, gadgetId, configId, CONF_UAC2)).exec();
        if (!res.isSuccess())
            throw new RuntimeException("Failed to enable UAC2!");
        uac2Enabled = true;
    }

    public void disableUAC2() {
        Shell.Result res;
        res = Shell.cmd(String.format("rm -rf /config/usb_gadget/%s/configs/%s/%s",
                    gadgetId, configId, CONF_UAC2)).exec();
        uac2Enabled = false;
    }

    public boolean uac2IsEnabled() {
        return uac2Enabled;
    }

    public void enableMassStorage() {
        // check if uac2 is already in the function folder
        List<String> func = this.getAvailableFunctions();
        funcMs = null;
        for (String s : func) {
            if (s.contains("mass_storage"))
                funcMs = s.trim();
        }

        // if not, mkdir
        if (funcMs == null) {
            funcMs = "mass_storage.0";
            if (!addFunction(funcMs))
                throw new RuntimeException("Mass storage is not supported on this device!");
        }

        // symbolic link function to config to enable uac2 function
        Shell.Result res = Shell.cmd(String.format("ln -s /config/usb_gadget/%s/functions/%s " +
                        "/config/usb_gadget/%s/configs/%s/%s",
                gadgetId, funcMs, gadgetId, configId, CONF_MS)).exec();
        if (!res.isSuccess())
            throw new RuntimeException("Failed to enable mass storage!");
        msEnabled = true;
    }

    public void disableMassStorage() {
        Shell.Result res;
        res = Shell.cmd(String.format("rm -rf /config/usb_gadget/%s/configs/%s/%s",
                    gadgetId, configId, CONF_MS)).exec();
        msEnabled = false;
    }

    public boolean msIsEnabled() {
        return msEnabled;
    }

    public void clearMassStorageFiles() {
        Shell.cmd(String.format("rm -rf /config/usb_gadget/%s/functions/%s/lun.*"), gadgetId, funcMs).exec();
    }

    public boolean setMassStorageFile(int index, String file) {
        if (index < 0) return false;
        Shell.Result res;
        res = Shell.cmd(String.format("ls /config/usb_gadget/%s/functions/%s", gadgetId, funcMs)).exec();
        if (res.isSuccess()) {
            // check if the lun with specified index exist
            String s = "";
            String indexStr = String.format("lun.%d", index);
            for (String s2: res.getOut()) {
                if (s2.equals(indexStr)) {
                    s = s2;
                    break;
                }
            }

            // if it does not exist, create lun with the specified index
            if (s.isEmpty()) {
                s = indexStr;
                res = Shell.cmd(String.format("ls /config/usb_gadget/%s/functions/%s/%s",
                        gadgetId, funcMs, s)).exec();
                if (!res.isSuccess()) return false;
            }

            // set file
            res = Shell.cmd(String.format("echo %s > /config/usb_gadget/%s/functions/%s/%s/file",
                    file, gadgetId, funcMs, s)).exec();
            return res.isSuccess();

        } else {
            return false;
        }
    }

    public void disableGadget() {
        Shell.Result res = Shell.cmd(String.format("echo \"\" > /config/usb_gadget/%s/UDC", gadgetId)).exec();
        if (!res.isSuccess())
            throw new RuntimeException(res.getOut().get(0));
    }

    public void enableGadget() {
        Shell.Result res = Shell.cmd(String.format("echo %s > /config/usb_gadget/%s/UDC", defaultUDC, gadgetId)).exec();
        if (!res.isSuccess())
            throw new RuntimeException(res.getOut().get(0));
    }

    public void resetUsb() {
        Shell.cmd(String.format("echo \"\" > /config/usb_gadget/%s/UDC", gadgetId)).exec();
        Shell.cmd(String.format("echo %s > /sys/bus/platform/drivers/ci_hdrc/unbind", defaultUDC)).exec();
        Shell.cmd(String.format("echo %s > /sys/bus/platform/drivers/ci_hdrc/bind", defaultUDC)).exec();
        Shell.cmd(String.format("echo %s > /config/usb_gadget/%s/UDC", defaultUDC, gadgetId)).exec();
    }



}
