sdata: fix formatting SD-ID with enterpriseID

Before fix: ".SDATA.test@1.2.3.log.fac=14" was formatted as "[test@1.2.3.log fac=14]"
The correct format is: "[test@1.2.3 log.fac=14]".

