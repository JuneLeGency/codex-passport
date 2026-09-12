package dev.passport.codex;

/** A comparison to an even budget across the reset window, not a usage forecast. */
public final class UsagePace {
    public final double elapsed, used, difference;
    public final String label;
    private UsagePace(double elapsed,double used){
        this.elapsed=elapsed;this.used=used;this.difference=used-elapsed;
        label=elapsed<1?"周期刚开始":difference>5?"用得偏快":difference< -5?"用得偏慢":"节奏均衡";
    }
    public static UsagePace at(int remaining,long minutes,long reset,long now){
        if(remaining<0||remaining>100||minutes<=0||minutes>5256000||now>=reset)return null;
        long duration=minutes*60,start=reset-duration;
        if(now<start)return null;
        return new UsagePace(100.0*(now-start)/duration,100-remaining);
    }
}
