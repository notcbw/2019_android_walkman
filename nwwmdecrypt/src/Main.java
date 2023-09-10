import com.sony.walkman.systemupdater.util.UpdateDataDecipher;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;

public class Main {
    private static void PrintHelpMsg() {
        System.out.println("Usage: java -jar nwwmdecrypt.jar -i <input file> -o <output file> -k <key string>");
    }

    public static void main(String[] args) {
        String inputFn = "";
        String outputFn = "";
        String keyString = "";

        if (args.length < 1) {
            System.out.println("No argument is provided!");
            PrintHelpMsg();
            System.exit(1);
        }

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "-h":
                    PrintHelpMsg();
                    System.exit(0);
                    break;
                case "-i":
                    i++;
                    if (i > args.length) {
                        PrintHelpMsg();
                        System.exit(1);
                    } else {
                        inputFn = args[i];
                    }
                    break;
                case "-o":
                    i++;
                    if (i > args.length) {
                        PrintHelpMsg();
                        System.exit(1);
                    } else {
                        outputFn = args[i];
                    }
                    break;
                case "-k":
                    i++;
                    if (i > args.length) {
                        PrintHelpMsg();
                        System.exit(1);
                    } else {
                        keyString = args[i];
                    }
                    break;
            }
        }

        if (inputFn.isBlank() || outputFn.isBlank() || keyString.isBlank()) {
            System.out.println("Bad arguments!");
            PrintHelpMsg();
            System.exit(1);
        }

        keyString = keyString.trim();
        if (keyString.length() != 48) {
            System.out.println("Wrong length of key! The key should be exactly 48 characters.");
            PrintHelpMsg();
            System.exit(1);
        }

        File in = new File(inputFn);
        File out = new File(outputFn);

        UpdateDataDecipher decipher = new UpdateDataDecipher(in, out, keyString);

        try {
            System.out.println("Decrypting...");
            decipher.decipher();
        } catch (UpdateDataDecipher.DecihperErrorException e) {
            System.out.println("Error during deciphering: " + e.toString());
            System.exit(-1);
        }
    }
}