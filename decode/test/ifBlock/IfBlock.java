public class IfBlock {
    private int argvs = 0;

    IfBlock(int s) {
        argvs = s;
    }

    public boolean is_positive() {
        if (argvs > 0) {
            return true;
        }
        return false;
    }

    public boolean is_negtive() {
        if (argvs < 0) {
            return true;
        }
        return false;
    }

    public static void main(String[] args) throws Exception {
        System.out.println("Hello World");
        IfBlock clt = new IfBlock(args.length);

        if (clt.is_positive()) {
            System.out.println("is_positive");
        } else if (clt.is_negtive()) {
            System.out.println("is_negtive");
        } else {
            System.out.println("is_zero");
        }

        System.out.println("Hello World");

        for (int i = 0; i < 10; i++) {
            System.out.println("Hello World");
        }
    }
}