import android.content.Context;
import android.os.PowerManager;
import android.os.Looper;
import java.io.InputStream;
import java.lang.reflect.Method;

// Debug-shell test helper. The lock has a deadline and binder death releases it.
public final class MaliCpuWakeProbe {
    public static void main(String[] args) throws Exception {
        int seconds = args.length >= 1 ? Integer.parseInt(args[0]) : 10;
        boolean hold = args.length < 2 || args[1].equals("on");
        if (args.length > 2 || (args.length == 2 && !hold && !args[1].equals("off"))) throw new IllegalArgumentException("invalid mode");
        if (seconds < 1 || seconds > 180) throw new IllegalArgumentException("invalid duration");
        Looper.prepareMainLooper();
        Class<?> cls = Class.forName("android.app.ActivityThread");
        Method systemMain = cls.getDeclaredMethod("systemMain");
        Object thread = systemMain.invoke(null);
        Context systemContext = (Context) cls.getDeclaredMethod("getSystemContext").invoke(thread);
        Context shellContext = systemContext.createPackageContext("com.android.shell", 0);
        PowerManager pm = (PowerManager) shellContext.getSystemService(Context.POWER_SERVICE);
        if (pm == null) throw new IllegalStateException("PowerManager unavailable");
        PowerManager.WakeLock lock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MaliResearch:CpuTest");
        lock.setReferenceCounted(false);
        try {
            if (hold) lock.acquire((seconds + 5L) * 1000L);
            System.out.println("MALI_WAKE_LOCK_MODE=" + (hold ? "on" : "off") + " HELD=" + lock.isHeld());
            System.out.flush();
            InputStream in = System.in;
            long until = System.currentTimeMillis() + seconds * 1000L;
            while (System.currentTimeMillis() < until) {
                if (in.available() > 0 && in.read() == 'q') break;
                Thread.sleep(200L);
            }
        } finally {
            if (lock.isHeld()) lock.release();
            System.out.println("MALI_WAKE_LOCK_RELEASED=" + !lock.isHeld());
            System.out.flush();
        }
    }
}
