// 3GPP LTE RLC: 4G Radio Link Control protocol interface
// Parameters for RLC AM


const char *const rlc_parameter_names[] =
{
  /* RLC mode AM/UM/TM */
  "rlc/mode",
  "rlc/debug",
  /* AM */
  "maxRetxThreshold",
  "amWindowSize",
  "pollPDU",
  "pollByte",
  "t-StatusProhibit",
  "t-PollRetransmit",
  /* AM & UM */
  "t-Reordering",
  /* UM */
  "SN-FieldLength.rx",
  "SN-FieldLength.tx",
  /* PDCP */
  "headerCompression",
  "pdcp-SN-Size",
  "statusReportRequired",
  "discardTimer",
  "maxCID",
  "profiles",
  "pdcp/t-Reordering",
  "",
  NULL
};

