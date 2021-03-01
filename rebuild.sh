

while getopts ":f:h" optname
do
  case "$optname" in
    "f")
      do_clean="y"
      ;;
    "h")
      echo "get option -h,eg:./rebuild.sh -f"
      exit
      ;;
    ":")
      case "$OPTARG" in
        "f")
          do_clean="y"
          ;;
        *)
          echo "No argument value for option $OPTARG"
          ;;
      esac
      ;;
    "?")
      echo "Unknown option $OPTARG"
      ;;
    *)
      echo "Unknown error while processing options"
      ;;
  esac
  #echo "option index is $OPTIND"
done

git pull
cd build

if [ -n "$do_clean" ]; then
    make clean
fi

make -j2
java -jar ../closure-compiler-v20201207.jar --compilation_level SIMPLE --js draco_decoder.js --js_output_file draco_wasm_wrapper.js
