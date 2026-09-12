import dev.passport.codex.UsagePace;
public final class UsagePaceTest {
    public static void main(String[] args){
        UsagePace p=UsagePace.at(25,100,12000,9000);
        if(p==null||p.elapsed!=50||p.used!=75||!p.label.equals("用得偏快"))throw new AssertionError("half time, three quarters consumed");
        if(!UsagePace.at(80,100,12000,9000).label.equals("用得偏慢"))throw new AssertionError("slow usage");
        if(!UsagePace.at(50,100,12000,9000).label.equals("节奏均衡"))throw new AssertionError("even usage");
        if(!UsagePace.at(100,100,12000,6000).label.equals("周期刚开始"))throw new AssertionError("window start");
        if(UsagePace.at(50,100,12000,12000)!=null||UsagePace.at(50,100,12000,5999)!=null||UsagePace.at(-1,100,12000,9000)!=null)throw new AssertionError("unavailable window");
        System.out.println("Usage pace boundary tests: PASS");
    }
}
