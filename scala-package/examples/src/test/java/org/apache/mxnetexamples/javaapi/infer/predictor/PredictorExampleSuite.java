package org.apache.mxnetexamples.javaapi.infer.predictor;

import org.junit.BeforeClass;
import org.junit.Test;
import org.apache.mxnetexamples.Util;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;

public class PredictorExampleSuite {

    final static Logger logger = LoggerFactory.getLogger(PredictorExampleSuite.class);
    private static String modelPathPrefix = "";
    private static String inputImagePath = "";

    @BeforeClass
    public static void downloadFile() {
        logger.info("Downloading resnet-18 model");

        String tempDirPath = System.getProperty("java.io.tmpdir");
        logger.info("tempDirPath: %s".format(tempDirPath));

        String baseUrl = "https://s3.us-east-2.amazonaws.com/scala-infer-models";

        Util.downloadUrl(baseUrl + "/resnet-18/resnet-18-symbol.json",
                tempDirPath + "/resnet18/resnet-18-symbol.json", 3);
        Util.downloadUrl(baseUrl + "/resnet-18/resnet-18-0000.params",
                tempDirPath + "/resnet18/resnet-18-0000.params", 3);
        Util.downloadUrl(baseUrl + "/resnet-18/synset.txt",
                tempDirPath + "/resnet18/synset.txt", 3);
        Util.downloadUrl("https://s3.amazonaws.com/model-server/inputs/Pug-Cookie.jpg",
                tempDirPath + "/inputImages/resnet18/Pug-Cookie.jpg", 3);

        modelPathPrefix = tempDirPath + File.separator + "resnet18/resnet-18";
        inputImagePath = tempDirPath + File.separator +
                "inputImages/resnet18/Pug-Cookie.jpg";
    }

    @Test
    public void testPredictor(){
        PredictorExample example = new PredictorExample();
        String[] args = new String[]{
                "--model-path-prefix", modelPathPrefix,
                "--input-image", inputImagePath
        };
        example.main(args);
    }

}
